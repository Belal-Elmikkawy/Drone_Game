#include "common.h"
#include <ncurses.h>
#include <time.h> 
#include <unistd.h> // for access()

// --- NETWORK HEADERS (Assignment 3 Requirement) ---
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// --- IPC PIPES ---
int pipe_input_to_server[2];
int pipe_server_to_drone[2];
int pipe_drone_to_server[2];
int pipe_obstacle_to_server[2];
int pipe_target_to_server[2];

// --- PROCESS PIDs ---
pid_t pid_input = -1, pid_drone = -1, pid_obs = -1, pid_tar = -1, pid_wd = -1;

// --- WORLD STATE ---
Point obstacles[MAX_OBSTACLES];
Point targets[MAX_TARGETS];
float drone_x, drone_y;
int screen_w = DEFAULT_WIDTH;
int screen_h = DEFAULT_HEIGHT;

// --- SCORING VARIABLES ---
int targets_collected = 0;
float total_distance = 0.0f;
time_t start_time;
int final_score = 0;

// Cyclic Buffer Indexes
int obs_idx = 0;
int tar_idx = 0;

// --- ASSIGNMENT 3 VARIABLES ---
int app_mode = MODE_STANDALONE; 
int sock_fd = -1;
Point remote_drone = {0, 0};
char server_ip[64] = "127.0.0.1";

// --- HELPER: RESOLVE PATH ---
const char* resolve_path(const char* path_a, const char* path_b, const char* path_c) {
    if (access(path_a, F_OK) == 0) return path_a;
    if (access(path_b, F_OK) == 0) return path_b;
    if (access(path_c, F_OK) == 0) return path_c;
    return path_a; 
}

// --- ASSIGNMENT 3: MODE SELECTION MENU ---
void get_user_mode() {
    remove("mode_selection.txt");

    FILE *script = fopen("launcher.sh", "w");
    if (!script) { perror("Failed to write script"); exit(1); }

    fprintf(script, "#!/bin/bash\n");
    fprintf(script, "while true; do\n");
    fprintf(script, "  echo '====================================='\n");
    fprintf(script, "  echo '   ASSIGNMENT 3: DRONE SETUP         '\n");
    fprintf(script, "  echo '====================================='\n");
    fprintf(script, "  echo 'Select Mode:'\n");
    fprintf(script, "  echo '  [S] SERVER  (Host)'\n");
    fprintf(script, "  echo '  [C] CLIENT  (Join)'\n");
    fprintf(script, "  echo '  [L] LOCAL   (Standalone)'\n");
    fprintf(script, "  read -p 'Choice: ' choice\n");
    fprintf(script, "  if [[ \"$choice\" == \"s\" || \"$choice\" == \"S\" ]]; then\n");
    fprintf(script, "      echo \"SERVER\" > mode_selection.txt; break\n");
    fprintf(script, "  elif [[ \"$choice\" == \"c\" || \"$choice\" == \"C\" ]]; then\n");
    fprintf(script, "      read -p 'Enter IP [127.0.0.1]: ' ip\n");
    fprintf(script, "      if [ -z \"$ip\" ]; then ip=\"127.0.0.1\"; fi\n");
    fprintf(script, "      echo \"CLIENT $ip\" > mode_selection.txt; break\n");
    fprintf(script, "  elif [[ \"$choice\" == \"l\" || \"$choice\" == \"L\" ]]; then\n");
    fprintf(script, "      echo \"LOCAL\" > mode_selection.txt; break\n");
    fprintf(script, "  fi\n");
    fprintf(script, "done\n");
    fclose(script);
    system("chmod +x launcher.sh");

    printf("Launching Configuration Menu...\n");
    system("xterm -T 'Drone Config' -geometry 60x15 -e ./launcher.sh &");

    printf("Waiting for user selection...\n");
    FILE *file = NULL;
    while (1) {
        file = fopen("mode_selection.txt", "r");
        if (file) break; 
        usleep(100000); 
    }

    char mode_str[32];
    fscanf(file, "%s", mode_str);
    
    if (strcmp(mode_str, "SERVER") == 0) app_mode = MODE_SERVER;
    else if (strcmp(mode_str, "CLIENT") == 0) {
        app_mode = MODE_CLIENT;
        fscanf(file, "%s", server_ip);
    } else {
        app_mode = MODE_STANDALONE;
    }
    
    fclose(file);
    remove("launcher.sh");
    remove("mode_selection.txt");
    sleep(1);
}

// --- ASSIGNMENT 3: NETWORK HELPERS ---
void send_net_msg(const char *str) {
    if (sock_fd < 0) return;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s\n", str);
    write(sock_fd, buf, strlen(buf));
}

int wait_ack(const char *expected) {
    char buf[128]; memset(buf, 0, sizeof(buf));
    int n = read(sock_fd, buf, sizeof(buf)-1);
    if (n <= 0) return 0;
    char *p = strchr(buf, '\n'); if (p) *p = 0;
    if (strncmp(buf, expected, strlen(expected)) == 0) return 1;
    return 0;
}

void to_virtual(int local_x, int local_y, int *virt_x, int *virt_y) {
    *virt_x = local_x;
    *virt_y = screen_h - local_y; 
}

void from_virtual(int virt_x, int virt_y, int *local_x, int *local_y) {
    *local_x = virt_x;
    *local_y = screen_h - virt_y;
}

void init_network() {
    if (app_mode == MODE_STANDALONE) return;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(NET_PORT);

    if (app_mode == MODE_SERVER) {
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        int opt = 1; setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        bind(sock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
        listen(sock_fd, 1);
        int newsock = accept(sock_fd, NULL, NULL);
        close(sock_fd); sock_fd = newsock;
    } else {
        inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);
        connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    }
    fcntl(sock_fd, F_SETFL, O_NONBLOCK);
}

void perform_handshake() {
    if (app_mode == MODE_STANDALONE) return;
    if (app_mode == MODE_SERVER) {
        send_net_msg("ok"); wait_ack("ook");
        char msg[64]; getmaxyx(stdscr, screen_h, screen_w); 
        snprintf(msg, sizeof(msg), "size %d %d", screen_w, screen_h);
        send_net_msg(msg); wait_ack("sok");
    } else {
        wait_ack("ok"); send_net_msg("ook");
        char buf[128]; int n = read(sock_fd, buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = 0; int w, h;
            if (sscanf(buf, "size %d %d", &w, &h) == 2) {
                screen_w = w; screen_h = h;
                resizeterm(screen_h, screen_w);
            }
        }
        send_net_msg("sok");
    }
}

// --- STANDARD FUNCTIONS ---

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
        perror("Failed to spawn xterm monitor");
        exit(0);
    }
}

void spawn_keyboard_guide() {
    pid_t p = fork();
    if (p == 0) {
        char cmd[1024];
        sprintf(cmd, "echo 'DRONE CONTROLS'; echo '[E][R] Up'; echo '[S][F] Left/Right'; echo '[X][V] Down'; echo '[SPACE] Brake'; tail -f %s", LOG_INPUT);
        execlp("xterm", "xterm", "-T", "CONTROLS", "-geometry", "50x25+0+0", "-e", "sh", "-c", cmd, NULL);
        perror("Failed to spawn keyboard guide");
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
    if (sock_fd > 0) { send_net_msg("q"); close(sock_fd); } // Network cleanup
    endwin(); 
    if (pid_input > 0) kill(pid_input, SIGKILL);
    if (pid_drone > 0) kill(pid_drone, SIGKILL);
    if (pid_obs > 0)   kill(pid_obs, SIGKILL);
    if (pid_tar > 0)   kill(pid_tar, SIGKILL);
    if (pid_wd > 0)    kill(pid_wd, SIGKILL);
}

void init_ncurses_safe() {
    initscr(); cbreak(); noecho(); curs_set(0); start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);    // Drone
    init_pair(2, COLOR_MAGENTA, COLOR_BLACK); // UI Borders
    init_pair(3, COLOR_GREEN, COLOR_BLACK);   // Obstacles
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);  // Targets
    init_pair(5, COLOR_RED, COLOR_BLACK);     // Remote Drone
}

void draw_ui(float fx, float fy) {
    erase();
    
    // Draw Borders & Stats
    attron(COLOR_PAIR(2));
    for(int x=0; x<screen_w; x++) { mvaddch(0, x, '-'); mvaddch(screen_h-1, x, '-'); }
    for(int y=0; y<screen_h; y++) { mvaddch(y, 0, '|'); mvaddch(y, screen_w-1, '|'); }
    
    mvprintw(0, 2, " Mode: %s | SCORE: %d | Targets: %d ", (app_mode==0)?"LOCAL":((app_mode==1)?"SERVER":"CLIENT"), final_score, targets_collected);
    mvprintw(screen_h-1, 2, " Cmd Force: %.1f, %.1f | Time: %lds | Dist: %.0fm ", fx, fy, time(NULL)-start_time, total_distance);
    attroff(COLOR_PAIR(2));

    // Draw Obstacles
    attron(COLOR_PAIR(3));
    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        if(obstacles[i].x > 0 && obstacles[i].x < screen_w && obstacles[i].y > 0 && obstacles[i].y < screen_h)
            mvaddch(obstacles[i].y, obstacles[i].x, 'O');
    }
    attroff(COLOR_PAIR(3));
    
    // Draw Remote Drone (As an Enemy/Obstacle)
    if (app_mode != MODE_STANDALONE && remote_drone.x != 0) {
        attron(COLOR_PAIR(5));
        mvaddch(remote_drone.y, remote_drone.x, 'X');
        attroff(COLOR_PAIR(5));
    }
    
    // Draw Targets (Only in Local Mode per assignment)
    if (app_mode == MODE_STANDALONE) {
        attron(COLOR_PAIR(4));
        for (int i = 0; i < MAX_TARGETS; ++i) {
            if(targets[i].x > 0 && targets[i].x < screen_w && targets[i].y > 0 && targets[i].y < screen_h)
                mvaddch(targets[i].y, targets[i].x, '1' + i);
        }
        attroff(COLOR_PAIR(4));
    }

    // Draw Local Drone
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
    for(int i=0; i<MAX_OBSTACLES; i++) 
        if(obstacles[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", obstacles[i].x, obstacles[i].y);
    
    // Inject Remote Drone as an obstacle so physics engine repels it
    if (app_mode != MODE_STANDALONE && remote_drone.x != 0) {
        offset += sprintf(msg + offset, "%d,%d;", remote_drone.x, remote_drone.y);
    }
    
    msg[offset-1] = '|'; 
    offset += sprintf(msg + offset, "T:");
    for(int i=0; i<MAX_TARGETS; i++) 
        if(targets[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", targets[i].x, targets[i].y);
    
    strcat(msg, "\n");
    dprintf(pipe_server_to_drone[1], "%s", msg);
}

int main(void) {
    srand(time(NULL)); 
    // --- [ASSIGNMENT 2 CORRECTION] ---
    // Clears logs at startup to prevent reading old data
    reset_logs();
    
    // 1. SELECT MODE
    get_user_mode();
    
    // --- [ASSIGNMENT 2 CORRECTION] ---
    // Registers the Server PID so it can be monitored
    register_process("Server");
    
    // 2. INIT NETWORK (If Server/Client selected)
    init_network();

    // 3. START GAME
    spawn_keyboard_guide(); 
    spawn_monitor("PHYSICS", LOG_DRONE, 400, 0); 
    spawn_monitor("GAME", LOG_GAME, 400, 300);
    spawn_monitor("WATCHDOG LOG", LOG_WATCHDOG, 400, 600); 

    init_world();
    start_time = time(NULL);

    // --- [BUG FIX] ERROR HANDLING FOR PIPES ---
    if (pipe(pipe_input_to_server) == -1) { perror("Failed to create pipe_input_to_server"); exit(1); }
    if (pipe(pipe_server_to_drone) == -1) { perror("Failed to create pipe_server_to_drone"); exit(1); }
    if (pipe(pipe_drone_to_server) == -1) { perror("Failed to create pipe_drone_to_server"); exit(1); }
    if (pipe(pipe_obstacle_to_server) == -1) { perror("Failed to create pipe_obstacle_to_server"); exit(1); }
    if (pipe(pipe_target_to_server) == -1) { perror("Failed to create pipe_target_to_server"); exit(1); }

    const char *p; 

    // --- LAUNCH PROCESSES ---
    
    // --- [ASSIGNMENT 2 CORRECTION] ---
    // In Server/Client mode, Obstacles, Targets, and Watchdog must be DISABLED.
    // We only launch them if we are in STANDALONE (Local) mode.
    if (app_mode == MODE_STANDALONE) {
        setup_watchdog_monitor("Server");
        
        if ((pid_wd = fork()) == 0) {
            p = resolve_path("src/watchdog/watchdog", "../watchdog/watchdog", "./watchdog");
            execlp("xterm", "xterm", "-T", "Watchdog Process", "-geometry", "40x10+0+0", "-e", p, NULL);
            perror("execlp watchdog failed"); 
            _exit(1);
        }
        sleep(1);

        if ((pid_obs = fork()) == 0) {
            dup2(pipe_obstacle_to_server[1], STDOUT_FILENO);
            close(pipe_obstacle_to_server[0]); close(pipe_obstacle_to_server[1]);
            p = resolve_path("src/obstacle/obstacle", "../obstacle/obstacle", "./obstacle");
            execl(p, "obstacle", NULL); 
            perror("execl obstacle failed");
            _exit(1);
        }

        if ((pid_tar = fork()) == 0) {
            dup2(pipe_target_to_server[1], STDOUT_FILENO);
            close(pipe_target_to_server[0]); close(pipe_target_to_server[1]);
            p = resolve_path("src/target/target", "../target/target", "./target");
            execl(p, "target", NULL); 
            perror("execl target failed");
            _exit(1);
        }
    }

    // Always launch Input and Drone
    if ((pid_input = fork()) == 0) {
        dup2(pipe_input_to_server[1], STDOUT_FILENO);
        close(pipe_input_to_server[0]); close(pipe_input_to_server[1]);
        p = resolve_path("src/input/input", "../input/input", "./input");
        execl(p, "input", NULL); 
        perror("execl input failed");
        _exit(1);
    }
    
    if ((pid_drone = fork()) == 0) {
        dup2(pipe_server_to_drone[0], STDIN_FILENO);
        dup2(pipe_drone_to_server[1], STDOUT_FILENO);
        close(pipe_server_to_drone[0]); close(pipe_server_to_drone[1]);
        close(pipe_drone_to_server[0]); close(pipe_drone_to_server[1]);
        p = resolve_path("src/drone/drone", "../drone/drone", "./drone");
        execl(p, "drone", NULL); 
        perror("execl drone failed");
        _exit(1);
    }

    // Close unused pipes
    close(pipe_input_to_server[1]); close(pipe_server_to_drone[0]); close(pipe_drone_to_server[1]);
    close(pipe_obstacle_to_server[1]); close(pipe_target_to_server[1]);

    // Set non-blocking I/O
    fcntl(pipe_input_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_drone_to_server[0], F_SETFL, O_NONBLOCK);
    if (app_mode == MODE_STANDALONE) {
        fcntl(pipe_obstacle_to_server[0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_target_to_server[0], F_SETFL, O_NONBLOCK);
    }

    init_ncurses_safe();
    perform_handshake(); 
    
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
        
        // Only listen to generated obstacles/targets in local mode
        if (app_mode == MODE_STANDALONE) {
            FD_SET(pipe_obstacle_to_server[0], &readfds);
            FD_SET(pipe_target_to_server[0], &readfds);
        }
        
        // Listen to Network in Multiplayer
        if (app_mode != MODE_STANDALONE && sock_fd > 0) {
            FD_SET(sock_fd, &readfds);
        }

        struct timeval timeout = {0, 20000};
        if (select(1024, &readfds, NULL, NULL, &timeout) < 0) {
            if (errno == EINTR) continue; 
            break;
        }

        set_status("Processing I/O");

        // 1. READ INPUT
        if (FD_ISSET(pipe_input_to_server[0], &readfds)) {
            int n = read(pipe_input_to_server[0], buf, sizeof(buf)-1);
            if (n == 0) break; 
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n'); 
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&force_x,&force_y); }
            }
        }

        // 2. READ OBSTACLES (Local Only)
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

        // 3. READ TARGETS (Local Only)
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
        
        // 4. NETWORK I/O (Multiplayer Only)
        if (app_mode != MODE_STANDALONE && FD_ISSET(sock_fd, &readfds)) {
            int n = read(sock_fd, buf, sizeof(buf)-1);
            if (n <= 0) break;
            buf[n] = 0;
            if (buf[0] == 'q') break; // Quit signal
            // Protocol: "d x y" for drone position
            if (buf[0] == 'd') {
                int vx, vy;
                if (sscanf(buf, "d %d %d", &vx, &vy) == 2) {
                    from_virtual(vx, vy, &remote_drone.x, &remote_drone.y);
                }
            }
        }

        send_state_to_drone(force_x, force_y);

        // 5. READ DRONE & UPDATE SCORE
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

                if (app_mode == MODE_STANDALONE) check_collisions();
                
                final_score = targets_collected * 1000;
                
                // MULTIPLAYER: Send my position to the other player
                if (app_mode != MODE_STANDALONE) {
                    int vx, vy;
                    to_virtual((int)drone_x, (int)drone_y, &vx, &vy);
                    char netmsg[64];
                    snprintf(netmsg, sizeof(netmsg), "d %d %d", vx, vy);
                    send_net_msg(netmsg);
                }
            }
        }
        draw_ui(force_x, force_y);
    }
    cleanup_processes();
    return 0;
}
