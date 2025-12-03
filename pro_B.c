// pro_B.c - Blackboard Server (Dashboard Version)
#include "common.h"
#include <ncurses.h>
#include <time.h> 

// Pipe FDs
int pipe_input_to_server[2];
int pipe_server_to_drone[2];
int pipe_drone_to_server[2];

pid_t pid_input = -1, pid_drone = -1;
volatile sig_atomic_t child_dead = 0;

Point obstacles[MAX_OBSTACLES];
Point targets[MAX_TARGETS];

float drone_x, drone_y;
int screen_w = DEFAULT_WIDTH;
int screen_h = DEFAULT_HEIGHT;

// --- NEW GLOBAL: Score ---
int score = 0;

// --- LOGGING & MONITORING ---

void reset_logs() {
    FILE *f;
    f = fopen(LOG_INPUT, "w"); if(f) { fprintf(f, "--- LIVE INPUT MONITOR ---\n"); fclose(f); }
    f = fopen(LOG_DRONE, "w"); if(f) { fprintf(f, "--- PHYSICS ENGINE LOG ---\n"); fclose(f); }
    f = fopen(LOG_GAME,  "w"); if(f) { fprintf(f, "--- GAME EVENTS ---\n"); fclose(f); }
}

// Spawns a standard log viewer
void spawn_monitor(const char *title, const char *logfile, int x, int y) {
    pid_t p = fork();
    if (p == 0) {
        char geometry[32]; sprintf(geometry, "90x15+%d+%d", x, y);
        execlp("xterm", "xterm", 
               "-T", title, 
               "-geometry", geometry, 
               "-e", "tail", "-f", logfile, 
               NULL);
        exit(0);
    }
}

// Spawns the Keyboard Guide + Input Log
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

        // Position it at Top-Left (0,0)
        execlp("xterm", "xterm", 
               "-T", "CONTROLS & INPUT",
               "-geometry", "50x25+0+0", 
               "-e", "sh", "-c", cmd, 
               NULL);
        exit(0);
    }
}

// --- GAME LOGIC ---

void log_game_state(float fx, float fy) {
    FILE *f = fopen(LOG_GAME, "a");
    if(f) {
        fprintf(f, "Drone: (%.1f, %.1f) | Cmd Force: (%.1f, %.1f)\n", drone_x, drone_y, fx, fy);
        fclose(f);
    }
}

// NEW FUNCTION: Check collisions and handle scoring
void check_collisions() {
    float radius = 2.0f; // Hit distance
    
    for(int i=0; i<MAX_TARGETS; i++) {
        if(targets[i].x == 0) continue;
        
        float dx = drone_x - targets[i].x;
        float dy = drone_y - targets[i].y;
        float dist = sqrt(dx*dx + dy*dy);

        if(dist < radius) {
            score++; 
            
            // Log the score event
            FILE *f = fopen(LOG_GAME, "a");
            if(f) { fprintf(f, ">>> TARGET HIT! Index: %d | New Score: %d\n", i, score); fclose(f); }

            // Respawn Target at a random location inside the screen
            targets[i].x = (rand() % (screen_w - 4)) + 2;
            targets[i].y = (rand() % (screen_h - 4)) + 2;
        }
    }
}

void init_world() {
    memset(obstacles, 0, sizeof(obstacles));
    memset(targets, 0, sizeof(targets));
    
    // Static Objects
    obstacles[0] = (Point){20, 10}; obstacles[1] = (Point){50, 15};
    obstacles[2] = (Point){80, 5};  obstacles[3] = (Point){30, 25};
    
    targets[0] = (Point){10, 5}; targets[1] = (Point){90, 20}; targets[2] = (Point){40, 10};
    
    FILE *f = fopen(LOG_GAME, "a");
    if(f) {
        fprintf(f, "World Initialized. Ready.\n");
        fclose(f);
    }
}

void sigchld_handler(int sig) { (void)sig; child_dead = 1; }

void cleanup_processes() {
    endwin();
    if (pid_input > 0) kill(pid_input, SIGKILL);
    if (pid_drone > 0) kill(pid_drone, SIGKILL);
}

void init_ncurses_safe() {
    initscr(); cbreak(); noecho(); curs_set(0); start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);    
    init_pair(2, COLOR_MAGENTA, COLOR_BLACK); 
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);  
    init_pair(4, COLOR_GREEN, COLOR_BLACK);   
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
    for (int i = 0; i < MAX_OBSTACLES; ++i) if(obstacles[i].x != 0) mvaddch(obstacles[i].y, obstacles[i].x, 'O');
    attroff(COLOR_PAIR(3));
    
    attron(COLOR_PAIR(4));
    for (int i = 0; i < MAX_TARGETS; ++i) if(targets[i].x != 0) mvaddch(targets[i].y, targets[i].x, '1' + i);
    attroff(COLOR_PAIR(4));

    attron(COLOR_PAIR(1));
    int dx = (int)drone_x; int dy = (int)drone_y;
    
    if (dx < 1) dx = 1; 
    if (dx >= screen_w-1) dx = screen_w-2;
    
    if (dy < 1) dy = 1; 
    if (dy >= screen_h-1) dy = screen_h-2;

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
    srand(time(NULL)); // Initialize random seed for respawning

    // 1. Setup Logs & Monitors
    reset_logs();
    spawn_keyboard_guide(); 
    spawn_monitor("PHYSICS DATA (Forces)", LOG_DRONE, 400, 0); 
    spawn_monitor("GAME EVENTS (State)", LOG_GAME, 400, 400); 

    init_world();

    if (pipe(pipe_input_to_server) == -1 || pipe(pipe_server_to_drone) == -1 || 
        pipe(pipe_drone_to_server) == -1) { perror("pipe"); exit(1); }
    signal(SIGCHLD, sigchld_handler);

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

    close(pipe_input_to_server[1]); 
    close(pipe_server_to_drone[0]); 
    close(pipe_drone_to_server[1]);

    fcntl(pipe_input_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_drone_to_server[0], F_SETFL, O_NONBLOCK);

    init_ncurses_safe();
    getmaxyx(stdscr, screen_h, screen_w);
    drone_x = screen_w/2.0f; drone_y = screen_h/2.0f;
    float force_x = 0, force_y = 0;
    char buf[BUF_SIZE];
    int maxfd = 100;
    fd_set readfds;

    while (1) {
        getmaxyx(stdscr, screen_h, screen_w);
        FD_ZERO(&readfds);
        FD_SET(pipe_input_to_server[0], &readfds);
        FD_SET(pipe_drone_to_server[0], &readfds);

        struct timeval timeout = {0, 20000};
        if (select(maxfd, &readfds, NULL, NULL, &timeout) < 0) { if (errno == EINTR) continue; break; }

        if (FD_ISSET(pipe_input_to_server[0], &readfds)) {
            int n = read(pipe_input_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n'); 
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&force_x,&force_y); }
            }
        }
        
        send_state_to_drone(force_x, force_y);

        if (FD_ISSET(pipe_drone_to_server[0], &readfds)) {
            int n = read(pipe_drone_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n'); 
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&drone_x,&drone_y); }
                log_game_state(force_x, force_y);
                
                // NEW: Check for collisions every time drone moves
                check_collisions();
            }
        }

        draw_ui(force_x, force_y);
        if (child_dead) break;
    }
    cleanup_processes();
    return 0;
}
