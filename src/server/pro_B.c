#include "../include/common.h"
#include <ncurses.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <math.h>

// --- IPC PIPES ---
int pipe_input_to_server[2];
int pipe_server_to_drone[2];
int pipe_drone_to_server[2];
int pipe_obstacle_to_server[2];
int pipe_target_to_server[2];

// Network Bridge Pipes
int pipe_server_to_net[2];
int pipe_net_to_server[2];

// --- PROCESS PIDs ---
pid_t pid_input = -1, pid_drone = -1, pid_obs = -1, pid_tar = -1, pid_wd = -1, pid_net = -1;

// --- WORLD STATE ---
Point obstacles[MAX_OBSTACLES];
Point targets[MAX_TARGETS];
float drone_x, drone_y;
int screen_w = DEFAULT_WIDTH;
int screen_h = DEFAULT_HEIGHT;

int app_mode = MODE_STANDALONE;
char server_ip[64] = "127.0.0.1";
char server_port[10] = "5555";

DroneState remote_drone_pos = {0,0};

// --- SCORING VARIABLES ---
int targets_collected = 0;
float total_distance = 0.0f;
int final_score = 0;
time_t start_time;

int obs_idx = 0;
int tar_idx = 0;

// --- FUNCTIONS ---

void reset_logs() {
    FILE *f;
    f = fopen(LOG_INPUT, "w"); if(f) { fprintf(f, "--- LIVE INPUT MONITOR ---\n"); fclose(f); }
    f = fopen(LOG_DRONE, "w"); if(f) { fprintf(f, "--- PHYSICS ENGINE LOG ---\n"); fclose(f); }
    f = fopen(LOG_GAME,  "w"); if(f) { fprintf(f, "--- GAME EVENTS ---\n"); fclose(f); }
    f = fopen(LOG_WATCHDOG,"w"); if(f) { fprintf(f, "--- WATCHDOG LOG ---\n"); fclose(f); }
    f = fopen(LOG_NETWORK,"w"); if(f) { fprintf(f, "--- NETWORK PROTOCOL LOG ---\n"); fclose(f); }
    f = fopen(FILE_PID, "w");  if(f) { fclose(f); }
}

void spawn_monitor(const char *title, const char *logfile, int x, int y) {
    pid_t p = fork();
    if (p == 0) {
        char geometry[32]; sprintf(geometry, "90x15+%d+%d", x, y);
        execlp("xterm", "xterm", "-T", title, "-geometry", geometry, "-e", "tail", "-F", logfile, NULL);
        exit(0);
    }
}

void spawn_keyboard_guide() {
    pid_t p = fork();
    if (p == 0) {
        char cmd[1024];
        sprintf(cmd, "echo 'DRONE CONTROLS'; echo '[E][R] Up'; echo '[S][F] Left/Right'; echo '[X][V] Down'; echo '[SPACE] Brake'; tail -F %s", LOG_INPUT);
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
    if (pid_net > 0)   kill(pid_net, SIGKILL);
}

void init_ncurses_safe() {
    initscr(); cbreak(); noecho(); curs_set(0); start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(3, COLOR_GREEN, COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
}

void get_user_mode() {
    endwin();
    printf("1. Server\n2. Client\n3. Local\nSelect: ");
    int c;
    if(scanf("%d", &c) != 1) c = 3;

    if(c == 1) { // SERVER
        app_mode = MODE_SERVER;
        printf("Hosting on Port (default 5555): ");
        char buf[10];
        getchar(); // Consume newline
        fgets(buf, sizeof(buf), stdin);
        if(isdigit(buf[0])) {
            buf[strcspn(buf, "\n")] = 0;
            strcpy(server_port, buf);
        }
    }
    else if(c == 2) { // CLIENT
        app_mode = MODE_CLIENT;
        printf("Server IP: "); scanf("%63s", server_ip);
        printf("Server Port: "); scanf("%9s", server_port);
    }
    else {
        app_mode = MODE_STANDALONE;
    }
}

void draw_ui(float fx, float fy) {
    erase();
    attron(COLOR_PAIR(2));
    box(stdscr, 0, 0);
    mvprintw(0, 2, " Mode: %s | Port: %s ",
             (app_mode==MODE_STANDALONE)?"LOCAL":((app_mode==MODE_SERVER)?"SERVER":"CLIENT"),
             (app_mode==MODE_STANDALONE)?"N/A":server_port);
    attroff(COLOR_PAIR(2));

    attron(COLOR_PAIR(3));
    for (int i = 0; i < MAX_OBSTACLES; ++i)
        if(obstacles[i].x>0) mvaddch(obstacles[i].y, obstacles[i].x, 'O');
    attroff(COLOR_PAIR(3));

    if(app_mode != MODE_STANDALONE && remote_drone_pos.x != 0) {
        attron(COLOR_PAIR(3));
        mvaddch((int)remote_drone_pos.y, (int)remote_drone_pos.x, 'X');
        attroff(COLOR_PAIR(3));
    }

    attron(COLOR_PAIR(4));
    for (int i = 0; i < MAX_TARGETS; ++i) {
        if(targets[i].x > 0) mvaddch(targets[i].y, targets[i].x, '1' + i);
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
    int offset = sprintf(msg, "W:%d,%d|F:%.2f,%.2f|O:", screen_w, screen_h, fx, fy);

    for(int i=0; i<MAX_OBSTACLES; i++)
        if(obstacles[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", obstacles[i].x, obstacles[i].y);

    if(app_mode != MODE_STANDALONE && remote_drone_pos.x != 0) {
        offset += sprintf(msg + offset, "%d,%d;", (int)remote_drone_pos.x, (int)remote_drone_pos.y);
    }

    msg[offset-1] = '|';
    offset += sprintf(msg + offset, "T:");
    for(int i=0; i<MAX_TARGETS; i++)
        if(targets[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", targets[i].x, targets[i].y);

    strcat(msg, "\n");
    dprintf(pipe_server_to_drone[1], "%s", msg);
}

// --- MAIN ---
int main(void) {
    srand(time(NULL));
    reset_logs();

    // 1. Register PID immediately
    register_process("Server");

    // 2. ASK USER FOR MODE *BEFORE* SETTING UP WATCHDOG
    get_user_mode();

    // 3. NOW check if we need the watchdog handler
    // If we are Server or Client, the Watchdog will be spawned later,
    // so we MUST register the signal handler now to prevent being killed.
    if(app_mode != MODE_STANDALONE) {
        setup_watchdog_monitor("Server");
    }

    // Create Pipes
    if (pipe(pipe_input_to_server) == -1)    { perror("Pipe Input"); exit(1); }
    if (pipe(pipe_server_to_drone) == -1)    { perror("Pipe S->D"); exit(1); }
    if (pipe(pipe_drone_to_server) == -1)    { perror("Pipe D->S"); exit(1); }
    if (pipe(pipe_obstacle_to_server) == -1) { perror("Pipe Obst"); exit(1); }
    if (pipe(pipe_target_to_server) == -1)   { perror("Pipe Targ"); exit(1); }

    // Network Pipes
    if(app_mode != MODE_STANDALONE) {
        if(pipe(pipe_server_to_net) == -1) { perror("Pipe NetTx"); exit(1); }
        if(pipe(pipe_net_to_server) == -1) { perror("Pipe NetRx"); exit(1); }
    }

    start_time = time(NULL);

    spawn_keyboard_guide();

    if (app_mode != MODE_STANDALONE) {
        spawn_monitor("PHYSICS", LOG_DRONE, 400, 0);
        spawn_monitor("NETWORK TRAFFIC", LOG_NETWORK, 800, 0);
    }

    init_world();

    // Spawn Watchdog (ONLY in Server/Client mode)
    if (app_mode != MODE_STANDALONE) {
        if ((pid_wd = fork()) == 0) {
            execlp("xterm", "xterm", "-T", "Watchdog", "-e", "./src/watchdog/watchdog", NULL);
            _exit(1);
        }
    }

    // Spawn Obstacles (Only in Standalone)
    if (app_mode == MODE_STANDALONE) {
        if ((pid_obs = fork()) == 0) {
            dup2(pipe_obstacle_to_server[1], STDOUT_FILENO);
            close(pipe_obstacle_to_server[0]); close(pipe_obstacle_to_server[1]);
            execl("src/obstacle/obstacle", "obstacle", NULL); _exit(1);
        }
        if ((pid_tar = fork()) == 0) {
            dup2(pipe_target_to_server[1], STDOUT_FILENO);
            close(pipe_target_to_server[0]); close(pipe_target_to_server[1]);
            execl("src/target/target", "target", NULL); _exit(1);
        }
    }

    // Spawn Network Bridge
    if (app_mode != MODE_STANDALONE) {
        if ((pid_net = fork()) == 0) {
            char mode_str[10], fd_rx[10], fd_tx[10];
            sprintf(mode_str, "%d", app_mode);
            sprintf(fd_rx, "%d", pipe_server_to_net[0]);
            sprintf(fd_tx, "%d", pipe_net_to_server[1]);

            close(pipe_server_to_net[1]); close(pipe_net_to_server[0]);

            // Redirect stderr to avoid UI corruption
            int null_fd = open("/dev/null", O_WRONLY);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);

            execl("src/network/network", "network", mode_str, fd_rx, fd_tx, server_port, server_ip, NULL);
            perror("Failed to spawn network bridge"); _exit(1);
        }
        close(pipe_server_to_net[0]); close(pipe_net_to_server[1]);
        fcntl(pipe_net_to_server[0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_server_to_net[1], F_SETFL, O_NONBLOCK); // Prevent Game Freeze if Network Stalls
    }

    // Spawn Input
    if ((pid_input = fork()) == 0) {
        dup2(pipe_input_to_server[1], STDOUT_FILENO);
        close(pipe_input_to_server[0]); close(pipe_input_to_server[1]);
        execl("src/input/input", "input", NULL); _exit(1);
    }

    // Spawn Drone
    if ((pid_drone = fork()) == 0) {
        dup2(pipe_server_to_drone[0], STDIN_FILENO);
        dup2(pipe_drone_to_server[1], STDOUT_FILENO);
        close(pipe_server_to_drone[0]); close(pipe_server_to_drone[1]);
        close(pipe_drone_to_server[0]); close(pipe_drone_to_server[1]);
        execl("src/drone/drone", "drone", NULL); _exit(1);
    }

    close(pipe_input_to_server[1]); close(pipe_server_to_drone[0]);
    close(pipe_drone_to_server[1]); close(pipe_obstacle_to_server[1]);
    close(pipe_target_to_server[1]);

    fcntl(pipe_input_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_drone_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_obstacle_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_target_to_server[0], F_SETFL, O_NONBLOCK);

    init_ncurses_safe();
    draw_ui(0, 0);

    float force_x = 0, force_y = 0;
    char buf[BUF_SIZE];
    fd_set readfds;

    // --- MAIN LOOP ---
    while (1) {
        getmaxyx(stdscr, screen_h, screen_w);
        FD_ZERO(&readfds);
        FD_SET(pipe_input_to_server[0], &readfds);
        FD_SET(pipe_drone_to_server[0], &readfds);

        if(app_mode == MODE_STANDALONE) {
            FD_SET(pipe_obstacle_to_server[0], &readfds);
            FD_SET(pipe_target_to_server[0], &readfds);
        }

        if(app_mode != MODE_STANDALONE) {
            FD_SET(pipe_net_to_server[0], &readfds);
        }

        struct timeval timeout = {0, 20000};
        if (select(1024, &readfds, NULL, NULL, &timeout) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (FD_ISSET(pipe_input_to_server[0], &readfds)) {
            int n = read(pipe_input_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n');
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&force_x,&force_y); }
            }
        }

        if (app_mode == MODE_STANDALONE && FD_ISSET(pipe_obstacle_to_server[0], &readfds)) {
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

        if (app_mode == MODE_STANDALONE && FD_ISSET(pipe_target_to_server[0], &readfds)) {
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

        if (app_mode != MODE_STANDALONE && FD_ISSET(pipe_net_to_server[0], &readfds)) {
            read(pipe_net_to_server[0], &remote_drone_pos, sizeof(DroneState));
        }

        if (app_mode != MODE_STANDALONE) {
            DroneState ds = {drone_x, drone_y};
            write(pipe_server_to_net[1], &ds, sizeof(DroneState));
        }

        send_state_to_drone(force_x, force_y);

        if (FD_ISSET(pipe_drone_to_server[0], &readfds)) {
            int n = read(pipe_drone_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n');
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&drone_x,&drone_y); }
                check_collisions();
                final_score = targets_collected * 1000;
            }
        }
        draw_ui(force_x, force_y);
    }
    cleanup_processes();
    return 0;
}
