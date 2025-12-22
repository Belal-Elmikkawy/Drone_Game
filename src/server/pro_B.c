#include "common.h"
#include <ncurses.h>
#include <time.h> 

// Pipes
int pipe_input_to_server[2];
int pipe_server_to_drone[2];
int pipe_drone_to_server[2];
int pipe_obstacle_to_server[2];
int pipe_target_to_server[2];

// PIDs
pid_t pid_input = -1, pid_drone = -1, pid_obs = -1, pid_tar = -1, pid_wd = -1;

Point obstacles[MAX_OBSTACLES];
Point targets[MAX_TARGETS];
float drone_x, drone_y;
int screen_w = DEFAULT_WIDTH;
int screen_h = DEFAULT_HEIGHT;

// Scoring Variables
int targets_collected = 0;
float total_distance = 0.0f;
time_t start_time;
int final_score = 0;

int obs_idx = 0;
int tar_idx = 0;

void reset_logs() {
    FILE *f;
    f = fopen(LOG_INPUT, "w"); if(f) { fprintf(f, "--- LIVE INPUT MONITOR ---\n"); fclose(f); }
    f = fopen(LOG_DRONE, "w"); if(f) { fprintf(f, "--- PHYSICS ENGINE LOG ---\n"); fclose(f); }
    f = fopen(LOG_GAME,  "w"); if(f) { fprintf(f, "--- GAME EVENTS ---\n"); fclose(f); }
    f = fopen(LOG_WATCHDOG,"w"); if(f) { fprintf(f, "--- WATCHDOG LOG ---\n"); fclose(f); }
    f = fopen(FILE_PID, "w");  if(f) { fclose(f); } 
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
        sprintf(cmd, "echo 'DRONE CONTROLS'; echo '[E][R] Up'; echo '[S][F] Left/Right'; echo '[X][V] Down'; echo '[SPACE] Brake'; tail -f %s", LOG_INPUT);
        execlp("xterm", "xterm", "-T", "CONTROLS", "-geometry", "50x25+0+0", "-e", "sh", "-c", cmd, NULL);
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
            targets_collected++; 
            targets[i].x = 0; targets[i].y = 0; 
            char msg[64];
            snprintf(msg, sizeof(msg), "SCORE! Target Collected. Total: %d", targets_collected);
            log_message(LOG_GAME, msg);
        }
    }
}

void init_world() {
    memset(obstacles, 0, sizeof(obstacles));
    memset(targets, 0, sizeof(targets));
}

void cleanup_processes() {
    endwin();
    if (pid_input > 0) kill(pid_input, SIGKILL);
    if (pid_drone > 0) kill(pid_drone, SIGKILL);
    if (pid_obs > 0)   kill(pid_obs, SIGKILL);
    if (pid_tar > 0)   kill(pid_tar, SIGKILL);
    if (pid_wd > 0)    kill(pid_wd, SIGKILL);
}

void init_ncurses_safe() {
    initscr(); cbreak(); noecho(); curs_set(0); start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);    
    init_pair(2, COLOR_MAGENTA, COLOR_BLACK); 
    init_pair(3, COLOR_GREEN, COLOR_BLACK);   
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);  
}

void draw_ui(float fx, float fy) {
    erase();
    attron(COLOR_PAIR(2));
    for(int x=0; x<screen_w; x++) { mvaddch(0, x, '-'); mvaddch(screen_h-1, x, '-'); }
    for(int y=0; y<screen_h; y++) { mvaddch(y, 0, '|'); mvaddch(y, screen_w-1, '|'); }
    mvprintw(0, 2, " Drone Sim | SCORE: %d | Targets: %d ", final_score, targets_collected);
    mvprintw(screen_h-1, 2, " Cmd Force: %.1f, %.1f | Time: %lds | Dist: %.0fm ", fx, fy, time(NULL)-start_time, total_distance);
    attroff(COLOR_PAIR(2));

    attron(COLOR_PAIR(3));
    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        if(obstacles[i].x > 0 && obstacles[i].x < screen_w && obstacles[i].y > 0 && obstacles[i].y < screen_h)
            mvaddch(obstacles[i].y, obstacles[i].x, 'O');
    }
    attroff(COLOR_PAIR(3));
    
    attron(COLOR_PAIR(4));
    for (int i = 0; i < MAX_TARGETS; ++i) {
        if(targets[i].x > 0 && targets[i].x < screen_w && targets[i].y > 0 && targets[i].y < screen_h)
            mvaddch(targets[i].y, targets[i].x, '1' + i);
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
    
    register_process("Server");
    setup_watchdog_monitor("Server");
    start_time = time(NULL);

    spawn_keyboard_guide(); 
    spawn_monitor("PHYSICS", LOG_DRONE, 400, 0); 
    spawn_monitor("GAME", LOG_GAME, 400, 300);
    spawn_monitor("WATCHDOG LOG", LOG_WATCHDOG, 400, 600); 

    init_world();

    if (pipe(pipe_input_to_server) == -1 || pipe(pipe_server_to_drone) == -1 || 
        pipe(pipe_drone_to_server) == -1 || pipe(pipe_obstacle_to_server) == -1 || 
        pipe(pipe_target_to_server) == -1) exit(1);

    // --- LAUNCH WATCHDOG (Updated Path) ---
    if ((pid_wd = fork()) == 0) {
        // Points to src/watchdog/watchdog
        execlp("xterm", "xterm", "-T", "Watchdog Process", "-geometry", "40x10+0+0", "-e", "src/watchdog/watchdog", NULL);
        _exit(1);
    }
    sleep(1);

    // --- LAUNCH INPUT (Updated Path) ---
    if ((pid_input = fork()) == 0) {
        dup2(pipe_input_to_server[1], STDOUT_FILENO);
        close(pipe_input_to_server[0]); close(pipe_input_to_server[1]);
        execl("src/input/input", "input", NULL); 
        _exit(1);
    }
    
    // --- LAUNCH DRONE (Updated Path) ---
    if ((pid_drone = fork()) == 0) {
        dup2(pipe_server_to_drone[0], STDIN_FILENO);
        dup2(pipe_drone_to_server[1], STDOUT_FILENO);
        close(pipe_server_to_drone[0]); close(pipe_server_to_drone[1]);
        close(pipe_drone_to_server[0]); close(pipe_drone_to_server[1]);
        execl("src/drone/drone", "drone", NULL); 
        _exit(1);
    }

    // --- LAUNCH OBSTACLES (Updated Path) ---
    if ((pid_obs = fork()) == 0) {
        dup2(pipe_obstacle_to_server[1], STDOUT_FILENO);
        close(pipe_obstacle_to_server[0]); close(pipe_obstacle_to_server[1]);
        execl("src/obstacle/obstacle", "obstacle", NULL); 
        _exit(1);
    }

    // --- LAUNCH TARGETS (Updated Path) ---
    if ((pid_tar = fork()) == 0) {
        dup2(pipe_target_to_server[1], STDOUT_FILENO);
        close(pipe_target_to_server[0]); close(pipe_target_to_server[1]);
        execl("src/target/target", "target", NULL); 
        _exit(1);
    }

    close(pipe_input_to_server[1]); close(pipe_server_to_drone[0]); close(pipe_drone_to_server[1]);
    close(pipe_obstacle_to_server[1]); close(pipe_target_to_server[1]);

    fcntl(pipe_input_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_drone_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_obstacle_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_target_to_server[0], F_SETFL, O_NONBLOCK);

    init_ncurses_safe();
    draw_ui(0, 0);

    float force_x = 0, force_y = 0;
    char buf[BUF_SIZE];
    fd_set readfds;

    while (1) {
        set_status("Main Loop Waiting");
        getmaxyx(stdscr, screen_h, screen_w);
        FD_ZERO(&readfds);
        FD_SET(pipe_input_to_server[0], &readfds);
        FD_SET(pipe_drone_to_server[0], &readfds);
        FD_SET(pipe_obstacle_to_server[0], &readfds);
        FD_SET(pipe_target_to_server[0], &readfds);

        struct timeval timeout = {0, 20000};
        if (select(1024, &readfds, NULL, NULL, &timeout) < 0) {
            if (errno == EINTR) continue; 
            break;
        }

        set_status("Processing I/O");

        if (FD_ISSET(pipe_input_to_server[0], &readfds)) {
            int n = read(pipe_input_to_server[0], buf, sizeof(buf)-1);
            if (n == 0) break; 
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n'); 
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&force_x,&force_y); }
            }
        }

        if (FD_ISSET(pipe_obstacle_to_server[0], &readfds)) {
            int n = read(pipe_obstacle_to_server[0], buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = 0; char *ptr = buf; int ox, oy, offset;
                while(sscanf(ptr, "%d,%d%n", &ox, &oy, &offset) == 2) {
                    obstacles[obs_idx].x = ox; obstacles[obs_idx].y = oy;
                    obs_idx = (obs_idx + 1) % MAX_OBSTACLES;
                    ptr += offset; while(*ptr == '\n' || *ptr == ' ' || *ptr == '\r') ptr++;
                }
            }
        }

        if (FD_ISSET(pipe_target_to_server[0], &readfds)) {
            int n = read(pipe_target_to_server[0], buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = 0; char *ptr = buf; int tx, ty, offset;
                while(sscanf(ptr, "%d,%d%n", &tx, &ty, &offset) == 2) {
                    targets[tar_idx].x = tx; targets[tar_idx].y = ty;
                    tar_idx = (tar_idx + 1) % MAX_TARGETS;
                    ptr += offset; while(*ptr == '\n' || *ptr == ' ' || *ptr == '\r') ptr++;
                }
            }
        }
        
        send_state_to_drone(force_x, force_y);

        if (FD_ISSET(pipe_drone_to_server[0], &readfds)) {
            int n = read(pipe_drone_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n'); 
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&drone_x,&drone_y); }
                
                static float last_x = -1, last_y = -1;
                if (last_x != -1) {
                    float dist_inc = sqrt(pow(drone_x - last_x, 2) + pow(drone_y - last_y, 2));
                    total_distance += dist_inc;
                }
                last_x = drone_x; last_y = drone_y;

                check_collisions();
                final_score = targets_collected * 100;
            }
        }
        draw_ui(force_x, force_y);
    }
    cleanup_processes();
    return 0;
}
