// pro_B.c - Server (Robust Parsing)
#include "common.h"
#include <ncurses.h>
#include <time.h> 

// Pipes
int pipe_input_to_server[2];
int pipe_server_to_drone[2];
int pipe_drone_to_server[2];
int pipe_obstacle_to_server[2];
int pipe_target_to_server[2];

pid_t pid_input = -1, pid_drone = -1, pid_obs = -1, pid_tar = -1;
volatile sig_atomic_t child_dead = 0;

Point obstacles[MAX_OBSTACLES];
Point targets[MAX_TARGETS];
float drone_x, drone_y;
int screen_w = DEFAULT_WIDTH;
int screen_h = DEFAULT_HEIGHT;
int score = 0;

// Cyclic Buffer Indexes
int obs_idx = 0;
int tar_idx = 0;

void reset_logs() {
    FILE *f;
    f = fopen(LOG_INPUT, "w"); if(f) { fprintf(f, "--- LIVE INPUT MONITOR ---\n"); fclose(f); }
    f = fopen(LOG_DRONE, "w"); if(f) { fprintf(f, "--- PHYSICS ENGINE LOG ---\n"); fclose(f); }
    f = fopen(LOG_GAME,  "w"); if(f) { fprintf(f, "--- GAME EVENTS ---\n"); fclose(f); }
}

void spawn_monitor(const char *title, const char *logfile, int x, int y) {
    pid_t p = fork();
    if (p == 0) {
        char geometry[32]; sprintf(geometry, "90x15+%d+%d", x, y);
        execlp("xterm", "xterm", "-T", title, "-geometry", geometry, "-e", "tail", "-f", logfile, NULL);
        exit(0);
    }
}

void spawn_keyboard_guide() {
    pid_t p = fork();
    if (p == 0) {
        char cmd[1024];
        sprintf(cmd, 
            "echo '=============================';"
            "echo '   DRONE CONTROL DASHBOARD   ';"
            "echo '=============================';"
            "echo ' ';"
            "echo '      [E]  [R]     (Up/Diag) ';"
            "echo '       ^    ^                ';"
            "echo ' [S] <---> [F]     (Left/Right)';"
            "echo '       v    v                ';"
            "echo '      [X]  [V]     (Down/Diag)';"
            "echo ' ';"
            "echo ' [ D ] or [SPACE]  -> BRAKE (Stop)';"
            "echo ' [ Q ]             -> QUIT';"
            "echo ' ';"
            "echo '-----------------------------';"
            "echo ' Live Key Press Log:';"
            "tail -f %s", LOG_INPUT);
        execlp("xterm", "xterm", "-T", "CONTROLS & INPUT", "-geometry", "50x25+0+0", "-e", "sh", "-c", cmd, NULL);
        exit(0);
    }
}

void check_collisions() {
    float radius = 2.0f;
    for(int i=0; i<MAX_TARGETS; i++) {
        if(targets[i].x == 0) continue;
        float dx = drone_x - targets[i].x;
        float dy = drone_y - targets[i].y;
        float dist = sqrt(dx*dx + dy*dy);
        if(dist < radius) {
            score++; 
            targets[i].x = 0; targets[i].y = 0; // Remove target
            FILE *f = fopen(LOG_GAME, "a");
            if(f) { fprintf(f, ">>> SCORE! Total: %d\n", score); fclose(f); }
        }
    }
}

void init_world() {
    memset(obstacles, 0, sizeof(obstacles));
    memset(targets, 0, sizeof(targets));
}

void sigchld_handler(int sig) { (void)sig; child_dead = 1; }

void cleanup_processes() {
    endwin();
    if (pid_input > 0) kill(pid_input, SIGKILL);
    if (pid_drone > 0) kill(pid_drone, SIGKILL);
    if (pid_obs > 0)   kill(pid_obs, SIGKILL);
    if (pid_tar > 0)   kill(pid_tar, SIGKILL);
}

void init_ncurses_safe() {
    initscr(); cbreak(); noecho(); curs_set(0); start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);    // Drone
    init_pair(2, COLOR_MAGENTA, COLOR_BLACK); // Border
    init_pair(3, COLOR_GREEN, COLOR_BLACK);   // Obstacles
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);  // Targets
}

void draw_ui(float fx, float fy) {
    erase();
    attron(COLOR_PAIR(2));
    for(int x=0; x<screen_w; x++) { mvaddch(0, x, '-'); mvaddch(screen_h-1, x, '-'); }
    for(int y=0; y<screen_h; y++) { mvaddch(y, 0, '|'); mvaddch(y, screen_w-1, '|'); }
    mvprintw(0, 2, " Drone Sim | SCORE: %d ", score);
    mvprintw(screen_h-1, 2, " Cmd Force: %.1f, %.1f ", fx, fy);
    attroff(COLOR_PAIR(2));

    attron(COLOR_PAIR(3));
    // Draw obstacles, checking bounds carefully
    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        if(obstacles[i].x > 0 && obstacles[i].x < screen_w && 
           obstacles[i].y > 0 && obstacles[i].y < screen_h) {
            mvaddch(obstacles[i].y, obstacles[i].x, 'O');
        }
    }
    attroff(COLOR_PAIR(3));
    
    attron(COLOR_PAIR(4));
    // Draw targets, checking bounds
    for (int i = 0; i < MAX_TARGETS; ++i) {
        if(targets[i].x > 0 && targets[i].x < screen_w && 
           targets[i].y > 0 && targets[i].y < screen_h) {
            mvaddch(targets[i].y, targets[i].x, '1' + i);
        }
    }
    attroff(COLOR_PAIR(4));

    attron(COLOR_PAIR(1));
    int dx = (int)drone_x; int dy = (int)drone_y;
    if (dx < 1) dx = 1; if (dx >= screen_w-1) dx = screen_w-2;
    if (dy < 1) dy = 1; if (dy >= screen_h-1) dy = screen_h-2;
    mvaddch(dy, dx, '+');
    attroff(COLOR_PAIR(1));
    refresh();
}

void send_state_to_drone(float fx, float fy) {
    char msg[BUF_SIZE];
    int offset = 0;
    offset += sprintf(msg + offset, "W:%d,%d|F:%.2f,%.2f|", screen_w, screen_h, fx, fy);
    offset += sprintf(msg + offset, "O:");
    for(int i=0; i<MAX_OBSTACLES; i++) if(obstacles[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", obstacles[i].x, obstacles[i].y);
    msg[offset-1] = '|'; 
    offset += sprintf(msg + offset, "T:");
    for(int i=0; i<MAX_TARGETS; i++) if(targets[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", targets[i].x, targets[i].y);
    strcat(msg, "\n");
    dprintf(pipe_server_to_drone[1], "%s", msg);
}

int main(void) {
    srand(time(NULL)); 
    reset_logs();
    spawn_keyboard_guide(); 
    spawn_monitor("PHYSICS DATA", LOG_DRONE, 400, 0); 
    spawn_monitor("GAME EVENTS", LOG_GAME, 400, 400); 

    init_world();

    // Create All Pipes
    if (pipe(pipe_input_to_server) == -1 || pipe(pipe_server_to_drone) == -1 || 
        pipe(pipe_drone_to_server) == -1 || pipe(pipe_obstacle_to_server) == -1 || 
        pipe(pipe_target_to_server) == -1) exit(1);
        
    signal(SIGCHLD, sigchld_handler);

    // Launch Processes
    if ((pid_input = fork()) == 0) {
        dup2(pipe_input_to_server[1], STDOUT_FILENO);
        close(pipe_input_to_server[0]); close(pipe_input_to_server[1]);
        execl("./input", "input", NULL); _exit(1);
    }
    if ((pid_drone = fork()) == 0) {
        dup2(pipe_server_to_drone[0], STDIN_FILENO);
        dup2(pipe_drone_to_server[1], STDOUT_FILENO);
        close(pipe_server_to_drone[0]); close(pipe_server_to_drone[1]);
        close(pipe_drone_to_server[0]); close(pipe_drone_to_server[1]);
        execl("./drone", "drone", NULL); _exit(1);
    }
    if ((pid_obs = fork()) == 0) {
        dup2(pipe_obstacle_to_server[1], STDOUT_FILENO);
        close(pipe_obstacle_to_server[0]); close(pipe_obstacle_to_server[1]);
        execl("./obstacle", "obstacle", NULL); _exit(1);
    }
    if ((pid_tar = fork()) == 0) {
        dup2(pipe_target_to_server[1], STDOUT_FILENO);
        close(pipe_target_to_server[0]); close(pipe_target_to_server[1]);
        execl("./target", "target", NULL); _exit(1);
    }

    close(pipe_input_to_server[1]); close(pipe_server_to_drone[0]); close(pipe_drone_to_server[1]);
    close(pipe_obstacle_to_server[1]); close(pipe_target_to_server[1]);

    // Set Non-blocking
    fcntl(pipe_input_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_drone_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_obstacle_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_target_to_server[0], F_SETFL, O_NONBLOCK);

    init_ncurses_safe();
    getmaxyx(stdscr, screen_h, screen_w);
    drone_x = screen_w/2.0f; drone_y = screen_h/2.0f;
    float force_x = 0, force_y = 0;
    char buf[BUF_SIZE];
    fd_set readfds;

    while (1) {
        getmaxyx(stdscr, screen_h, screen_w);
        FD_ZERO(&readfds);
        FD_SET(pipe_input_to_server[0], &readfds);
        FD_SET(pipe_drone_to_server[0], &readfds);
        FD_SET(pipe_obstacle_to_server[0], &readfds);
        FD_SET(pipe_target_to_server[0], &readfds);

        struct timeval timeout = {0, 20000};
        if (select(1024, &readfds, NULL, NULL, &timeout) < 0) break;

        // 1. INPUT
        if (FD_ISSET(pipe_input_to_server[0], &readfds)) {
            int n = read(pipe_input_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n'); 
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&force_x,&force_y); }
            }
        }

        // 2. OBSTACLES (Correct Loop Parsing)
        if (FD_ISSET(pipe_obstacle_to_server[0], &readfds)) {
            int n = read(pipe_obstacle_to_server[0], buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = 0;
                int ox, oy, offset;
                char *ptr = buf;
                // Read every pair in the buffer
                while(sscanf(ptr, "%d,%d%n", &ox, &oy, &offset) == 2) {
                    obstacles[obs_idx].x = ox; 
                    obstacles[obs_idx].y = oy;
                    obs_idx = (obs_idx + 1) % MAX_OBSTACLES;
                    ptr += offset;
                    // Skip delimiters (newline, space, comma)
                    while(*ptr == '\n' || *ptr == ' ' || *ptr == '\r') ptr++;
                }
            }
        }

        // 3. TARGETS (Correct Loop Parsing)
        if (FD_ISSET(pipe_target_to_server[0], &readfds)) {
            int n = read(pipe_target_to_server[0], buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = 0;
                int tx, ty, offset;
                char *ptr = buf;
                while(sscanf(ptr, "%d,%d%n", &tx, &ty, &offset) == 2) {
                    targets[tar_idx].x = tx; 
                    targets[tar_idx].y = ty;
                    tar_idx = (tar_idx + 1) % MAX_TARGETS;
                    ptr += offset;
                    while(*ptr == '\n' || *ptr == ' ' || *ptr == '\r') ptr++;
                }
            }
        }
        
        send_state_to_drone(force_x, force_y);

        // 4. DRONE
        if (FD_ISSET(pipe_drone_to_server[0], &readfds)) {
            int n = read(pipe_drone_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n'); 
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&drone_x,&drone_y); }
                check_collisions();
            }
        }
        draw_ui(force_x, force_y);
        if (child_dead) break;
    }
    cleanup_processes();
    return 0;
}
