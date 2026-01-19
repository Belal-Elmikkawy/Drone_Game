/*
 * BLACKBOARD PROCESS (Server / Master)
 * ------------------------------------
 * This is the central hub of the Blackboard Architecture.
 * Responsibilities:
 * 1. Orchestration: Spawns all child processes (Input, Drone, Obstacles, Watchdog, Network).
 * 2. IPC Routing: Routes messages between processes via pipes.
 * 3. Rendering: Draws the UI using ncurses.
 * 4. Game Logic: Maintains valid world state (Score, Collisions).
 */

#include "../include/common.h"
#include <ncurses.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <math.h>

// --- IPC PIPE FILE DESCRIPTORS ---
// Pipes are Unidirectional. We need pairs for bidirectional communication.
int pipe_input_to_server[2];    // Input Process -> Server
int pipe_server_to_drone[2];    // Server -> Drone Process (Env Data)
int pipe_drone_to_server[2];    // Drone Process -> Server (Position)
int pipe_obstacle_to_server[2]; // Obstacle Gen -> Server
int pipe_target_to_server[2];   // Target Gen -> Server

// Network Bridge Pipes (Assignment 3)
int pipe_server_to_net[2];      // Server -> Network (Tx)
int pipe_net_to_server[2];      // Network -> Server (Rx)

// --- CHILD PROCESS PIDS ---
// Used to manage lifecycle (spawn/kill) of children
pid_t pid_input = -1, pid_drone = -1, pid_obs = -1, pid_tar = -1, pid_wd = -1, pid_net = -1;

// --- WORLD STATE ---
Point obstacles[MAX_OBSTACLES];
Point targets[MAX_TARGETS];
float drone_x, drone_y;        // Local Drone Position
int screen_w = DEFAULT_WIDTH;
int screen_h = DEFAULT_HEIGHT;

int app_mode = MODE_STANDALONE;
char server_ip[64] = "127.0.0.1";
char server_port[10] = "5555";

DroneState remote_drone_pos = {0,0}; // Position of player 2 (Network)

// --- GAME STATS ---
int targets_collected = 0;
float total_distance = 0.0f; // Unused but reserved for future
int final_score = 0;
time_t start_time;

int obs_idx = 0; // Ring buffer index for obstacles
int tar_idx = 0; // Ring buffer index for targets

// --- HELPER FUNCTIONS ---

/*
 * reset_logs
 * ----------
 * Clears old log files at startup to ensure clean debugging.
 */
/*
 * reset_logs
 * ----------
 * Clears old log files at startup to ensure clean debugging.
 * This function is critical for Assignment 2 logging requirements.
 * It opens each declared log file in 'write' mode (clearing context)
 * and writes a header line.
 */
void reset_logs() {
    FILE *f;
    f = fopen(LOG_INPUT, "w"); if(f) { fprintf(f, "--- LIVE INPUT MONITOR ---\n"); fclose(f); }
    f = fopen(LOG_DRONE, "w"); if(f) { fprintf(f, "--- PHYSICS ENGINE LOG ---\n"); fclose(f); }
    f = fopen(LOG_GAME,  "w"); if(f) { fprintf(f, "--- GAME EVENTS ---\n"); fclose(f); }
    f = fopen(LOG_WATCHDOG,"w"); if(f) { fprintf(f, "--- WATCHDOG LOG ---\n"); fclose(f); }
    f = fopen(LOG_NETWORK,"w"); if(f) { fprintf(f, "--- NETWORK PROTOCOL LOG ---\n"); fclose(f); }
    f = fopen(FILE_PID, "w");  if(f) { fclose(f); }
}

/*
 * spawn_monitor
 * -------------
 * Spawns a new xterm window executing 'tail -f' on a log file.
 * This provides real-time visibility into specific subsystems.
 */
void spawn_monitor(const char *title, const char *logfile, int x, int y) {
    pid_t p = fork();
    if (p == 0) {
        char geometry[32]; sprintf(geometry, "90x15+%d+%d", x, y);
        execlp("xterm", "xterm", "-T", title, "-geometry", geometry, "-e", "tail", "-F", logfile, NULL);
        exit(0);
    }
}

/*
 * spawn_keyboard_guide
 * --------------------
 * Spawns a help window showing valid keys.
 */
void spawn_keyboard_guide() {
    pid_t p = fork();
    if (p == 0) {
        char cmd[1024];
        sprintf(cmd, "echo 'DRONE CONTROLS'; echo '[E][R] Up'; echo '[S][F] Left/Right'; echo '[X][V] Down'; echo '[SPACE] Brake'; tail -F %s", LOG_INPUT);
        execlp("xterm", "xterm", "-T", "CONTROLS", "-geometry", "50x25+0+0", "-e", "sh", "-c", cmd, NULL);
        exit(0);
    }
}

/*
 * check_collisions
 * ----------------
 * Checks if Drone overlaps any active Target.
 * Updates score if collision detected.
 *
 * Algorithm description:
 * - Iterates through all targets.
 * - Calculates Euclidean distance.
 * - If distance < 2.0 (Collision Radius), collects target.
 */
void check_collisions() {
    float radius = 2.0f;
    for(int i=0; i<MAX_TARGETS; i++) {
        if(targets[i].x == 0) continue; // Skip inactive targets
        float dx = drone_x - targets[i].x;
        float dy = drone_y - targets[i].y;
        float dist = sqrt(dx*dx + dy*dy);
        if(dist < radius) {
            targets_collected++;
            targets[i].x = 0; targets[i].y = 0; // Despawn target
            char msg[64];
            snprintf(msg, sizeof(msg), "SCORE! Target Collected. Total: %d", targets_collected);
            log_message(LOG_GAME, msg);
        }
    }
}

/*
 * init_world
 * ----------
 * Resets the game state (clears obstacles and targets).
 */
void init_world() {
    memset(obstacles, 0, sizeof(obstacles));
    memset(targets, 0, sizeof(targets));
}

/*
 * cleanup_processes
 * -----------------
 * Sends SIGKILL to all children on exit logic.
 * Ensures no orphan processes are left running.
 */
void cleanup_processes() {
    endwin(); // detailed Ncurses shutdown
    if (pid_input > 0) kill(pid_input, SIGKILL);
    if (pid_drone > 0) kill(pid_drone, SIGKILL);
    if (pid_obs > 0)   kill(pid_obs, SIGKILL);
    if (pid_tar > 0)   kill(pid_tar, SIGKILL);
    if (pid_wd > 0)    kill(pid_wd, SIGKILL);
    if (pid_net > 0)   kill(pid_net, SIGKILL);
}

/*
 * init_ncurses_safe
 * -----------------
 * Initializes the ncurses library for text-based UI.
 */
void init_ncurses_safe() {
    initscr(); cbreak(); noecho(); curs_set(0); start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);    // Drone
    init_pair(2, COLOR_MAGENTA, COLOR_BLACK); // Border/UI
    init_pair(3, COLOR_GREEN, COLOR_BLACK);   // Obstacles
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);  // Targets
}

/*
 * get_user_mode
 * -------------
 * Prompts user for Game Mode (Server/Client/Local) at startup.
 * Inputs are read from stdin before ncurses takes over.
 */
void get_user_mode() {
    endwin(); // Temporarily exit ncurses if active (not yet)
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

/*
 * draw_ui
 * -------
 * MAIN RENDER FUNCTION
 * Draws the border, stats, entities, and local/remote drones.
 *
 * input: fx, fy (Current input forces to display for debugging)
 */
void draw_ui(float fx, float fy) {
    erase();

    // Draw Border & Header
    attron(COLOR_PAIR(2));
    box(stdscr, 0, 0);
    mvprintw(0, 2, " Mode: %s | Port: %s ",
             (app_mode==MODE_STANDALONE)?"LOCAL":((app_mode==MODE_SERVER)?"SERVER":"CLIENT"),
             (app_mode==MODE_STANDALONE)?"N/A":server_port);
    attroff(COLOR_PAIR(2));

    // Draw Obstacles
    attron(COLOR_PAIR(3));
    for (int i = 0; i < MAX_OBSTACLES; ++i)
        if(obstacles[i].x>0) mvaddch(obstacles[i].y, obstacles[i].x, 'O');
    attroff(COLOR_PAIR(3));

    // Draw Remote Drone (Player 2)
    if(app_mode != MODE_STANDALONE && remote_drone_pos.x != 0) {
        attron(COLOR_PAIR(3));
        // Note: remote_drone_pos.y is already corrected/inverted in main loop
        mvaddch((int)remote_drone_pos.y, (int)remote_drone_pos.x, 'X');
        attroff(COLOR_PAIR(3));
    }

    // Draw Targets
    attron(COLOR_PAIR(4));
    for (int i = 0; i < MAX_TARGETS; ++i) {
        if(targets[i].x > 0) mvaddch(targets[i].y, targets[i].x, '1' + i);
    }
    attroff(COLOR_PAIR(4));

    // Draw Local Drone (Player 1)
    attron(COLOR_PAIR(1));
    int dx = (int)drone_x; int dy = (int)drone_y;
    // Boundary Clamps (Prevent drawing outside window)
    if (dx < 1) dx = 1;
    if (dx >= screen_w-1) dx = screen_w-2;
    if (dy < 1) dy = 1;
    if (dy >= screen_h-1) dy = screen_h-2;
    mvaddch(dy, dx, '+');
    attroff(COLOR_PAIR(1));

    refresh();
}

/*
 * send_state_to_drone
 * -------------------
 * Constructs the Environment String to send to the Physics Engine.
 * Format: "W:w,h|F:fx,fy|O:ox,oy;ox,oy...|T:tx,ty..."
 *
 * Key Logic:
 * - Also appends Remote Drone as an Obstacle ('O') so the local Physics Engine
 *   automatically avoids it using the same repulsion logic.
 */
void send_state_to_drone(float fx, float fy) {
    char msg[BUF_SIZE];
    int offset = sprintf(msg, "W:%d,%d|F:%.2f,%.2f|O:", screen_w, screen_h, fx, fy);

    // Append standard obstacles
    for(int i=0; i<MAX_OBSTACLES; i++)
        if(obstacles[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", obstacles[i].x, obstacles[i].y);

    // Append Remote Drone as Obstacle (Behavior Injection)
    if(app_mode != MODE_STANDALONE && remote_drone_pos.x != 0) {
        offset += sprintf(msg + offset, "%d,%d;", (int)remote_drone_pos.x, (int)remote_drone_pos.y);
    }

    msg[offset-1] = '|'; // Replace last semicolon or colon
    if (msg[offset-1] == ':') offset++; // Restore if it was empty

    offset += sprintf(msg + offset, "T:");
    for(int i=0; i<MAX_TARGETS; i++)
        if(targets[i].x != 0) offset += sprintf(msg + offset, "%d,%d;", targets[i].x, targets[i].y);

    strcat(msg, "\n");
    dprintf(pipe_server_to_drone[1], "%s", msg); // Send to Physics Pipe
}

// --- MAIN EXECUTION ---
int main(void) {
    srand(time(NULL));
    reset_logs();

    // 1. Identification
    register_process("Server");

    // 2. Select Mode
    get_user_mode();

    // 3. Register with Watchdog (Before loop starts)
    if(app_mode != MODE_STANDALONE) {
        setup_watchdog_monitor("Server");
    }

    // 4. Create Pipes (Standard)
    if (pipe(pipe_input_to_server) == -1)    { perror("Pipe Input"); exit(1); }
    if (pipe(pipe_server_to_drone) == -1)    { perror("Pipe S->D"); exit(1); }
    if (pipe(pipe_drone_to_server) == -1)    { perror("Pipe D->S"); exit(1); }
    if (pipe(pipe_obstacle_to_server) == -1) { perror("Pipe Obst"); exit(1); }
    if (pipe(pipe_target_to_server) == -1)   { perror("Pipe Targ"); exit(1); }

    // 5. Create Network Pipes
    if(app_mode != MODE_STANDALONE) {
        if(pipe(pipe_server_to_net) == -1) { perror("Pipe NetTx"); exit(1); }
        if(pipe(pipe_net_to_server) == -1) { perror("Pipe NetRx"); exit(1); }
    }

    start_time = time(NULL);

    spawn_keyboard_guide();

    // Open extra log monitors in Multiplayer mode
    if (app_mode != MODE_STANDALONE) {
        spawn_monitor("PHYSICS", LOG_DRONE, 400, 0);
        spawn_monitor("NETWORK TRAFFIC", LOG_NETWORK, 800, 0);
    }

    init_world();

    // --- SPAWNING CHILD PROCESSES ---

    // A) Watchdog (Only in Multiplayer)
    if (app_mode != MODE_STANDALONE) {
        if ((pid_wd = fork()) == 0) {
            execlp("xterm", "xterm", "-T", "Watchdog", "-e", "./src/watchdog/watchdog", NULL);
            _exit(1);
        }
    }

    // B) Generators (Only in Local Mode - Multiplayer has no random obstacles)
    if (app_mode == MODE_STANDALONE) {
        if ((pid_obs = fork()) == 0) {
            dup2(pipe_obstacle_to_server[1], STDOUT_FILENO); // Pipe Stdout -> Server
            close(pipe_obstacle_to_server[0]); close(pipe_obstacle_to_server[1]);
            execl("src/obstacle/obstacle", "obstacle", NULL); _exit(1);
        }
        if ((pid_tar = fork()) == 0) {
            dup2(pipe_target_to_server[1], STDOUT_FILENO);
            close(pipe_target_to_server[0]); close(pipe_target_to_server[1]);
            execl("src/target/target", "target", NULL); _exit(1);
        }
    }

    // C) Network Bridge (The TCP Handler)
    if (app_mode != MODE_STANDALONE) {
        if ((pid_net = fork()) == 0) {
            char mode_str[10], fd_rx[10], fd_tx[10];
            sprintf(mode_str, "%d", app_mode);
            sprintf(fd_rx, "%d", pipe_server_to_net[0]);
            sprintf(fd_tx, "%d", pipe_net_to_server[1]);

            close(pipe_server_to_net[1]); close(pipe_net_to_server[0]);

            // SILENCE STDERR of Network Process to prevent UI Corruption
            int null_fd = open("/dev/null", O_WRONLY);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);

            execl("src/network/network", "network", mode_str, fd_rx, fd_tx, server_port, server_ip, NULL);
            perror("Failed to spawn network bridge"); _exit(1);
        }
        close(pipe_server_to_net[0]); close(pipe_net_to_server[1]);
        fcntl(pipe_net_to_server[0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_server_to_net[1], F_SETFL, O_NONBLOCK);
    }

    // D) Input Process
    if ((pid_input = fork()) == 0) {
        dup2(pipe_input_to_server[1], STDOUT_FILENO);
        close(pipe_input_to_server[0]); close(pipe_input_to_server[1]);
        execl("src/input/input", "input", NULL); _exit(1);
    }

    // E) Drone Physics Process
    if ((pid_drone = fork()) == 0) {
        dup2(pipe_server_to_drone[0], STDIN_FILENO); // Reads Env from Server
        dup2(pipe_drone_to_server[1], STDOUT_FILENO); // Writes State to Server
        close(pipe_server_to_drone[0]); close(pipe_server_to_drone[1]);
        close(pipe_drone_to_server[0]); close(pipe_drone_to_server[1]);
        execl("src/drone/drone", "drone", NULL); _exit(1);
    }

    // Close unused ends in Parent
    close(pipe_input_to_server[1]); close(pipe_server_to_drone[0]);
    close(pipe_drone_to_server[1]); close(pipe_obstacle_to_server[1]);
    close(pipe_target_to_server[1]);

    // Set Read ends to Non-Blocking
    fcntl(pipe_input_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_drone_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_obstacle_to_server[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_target_to_server[0], F_SETFL, O_NONBLOCK);

    init_ncurses_safe();
    draw_ui(0, 0);

    float force_x = 0, force_y = 0;
    char buf[BUF_SIZE];
    fd_set readfds;

    // --- MAIN EVENT LOOP ---
    while (1) {
        getmaxyx(stdscr, screen_h, screen_w); // Handle Resize
        FD_ZERO(&readfds);

        // Add Pipes to Set
        FD_SET(pipe_input_to_server[0], &readfds);
        FD_SET(pipe_drone_to_server[0], &readfds);

        if(app_mode == MODE_STANDALONE) {
            FD_SET(pipe_obstacle_to_server[0], &readfds);
            FD_SET(pipe_target_to_server[0], &readfds);
        }

        if(app_mode != MODE_STANDALONE) {
            FD_SET(pipe_net_to_server[0], &readfds);
        }

        // Wait for Activity (20ms Timeout = ~50 FPS)
        struct timeval timeout = {0, 20000};
        if (select(1024, &readfds, NULL, NULL, &timeout) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // 1. Process Input
        if (FD_ISSET(pipe_input_to_server[0], &readfds)) {
            int n = read(pipe_input_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n');
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&force_x,&force_y); }
            }
        }

        // 3. Process Obstacles (Assignment 2 Despawn Logic)
        if (app_mode == MODE_STANDALONE && FD_ISSET(pipe_obstacle_to_server[0], &readfds)) {
            int n = read(pipe_obstacle_to_server[0], buf, sizeof(buf)-1);
            if (n > 0) {
                 // Fast parsing of multiple coordinate pairs
                buf[n] = 0; char *ptr = buf; int ox, oy, offset;
                while(sscanf(ptr, "%d,%d%n", &ox, &oy, &offset) == 2) {

                    if (ox == 0 && oy == 0) {
                        // --- DESPAWN EVENT (Assignment 2) ---
                        // The Generator requested a despawn (0,0).
                        // We find a random active obstacle and remove it.
                        // We check up to MAX_OBSTACLES times to find a non-zero one.
                        int attempts = MAX_OBSTACLES;
                        int r_idx = rand() % MAX_OBSTACLES;
                        while (attempts-- > 0) {
                             if (obstacles[r_idx].x != 0) {
                                 obstacles[r_idx].x = 0; obstacles[r_idx].y = 0; // Clear it
                                 break;
                             }
                             r_idx = (r_idx + 1) % MAX_OBSTACLES;
                        }
                    } else {
                        // --- SPAWN EVENT ---
                        // Standard spawn logic using Ring Buffer
                        obstacles[obs_idx].x = ox; obstacles[obs_idx].y = oy;
                        obs_idx = (obs_idx + 1) % MAX_OBSTACLES; // Ring buffer
                    }

                    ptr += offset; while(*ptr == '\n' || *ptr == ' ' || *ptr == '\r') ptr++;
                }
            }
        }

        // 3. Process Targets
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

        // 4. Process Network (Remote Drone)
        if (app_mode != MODE_STANDALONE && FD_ISSET(pipe_net_to_server[0], &readfds)) {
            read(pipe_net_to_server[0], &remote_drone_pos, sizeof(DroneState));
            // Fix inverted movement locally (User Request)
            // Flip the Y-coordinate coming from the network to match local screen space
            if(remote_drone_pos.x != 0) {
                remote_drone_pos.y = (float)(screen_h - 1) - remote_drone_pos.y;
            }
        }

        // 5. Send Local Position to Network
        if (app_mode != MODE_STANDALONE) {
            DroneState ds = {drone_x, drone_y};
            write(pipe_server_to_net[1], &ds, sizeof(DroneState));
        }

        // 6. Update Physics Engine
        send_state_to_drone(force_x, force_y);

        // 7. Get New Physics State
        if (FD_ISSET(pipe_drone_to_server[0], &readfds)) {
            int n = read(pipe_drone_to_server[0], buf, sizeof(buf)-1);
            if(n>0) {
                buf[n]=0; char* l=strrchr(buf,'\n');
                if(l){ *l=0; char* s=strrchr(buf,'\n'); s=(s)?s+1:buf; sscanf(s,"%f,%f",&drone_x,&drone_y); }
                check_collisions();
                final_score = targets_collected * 1000;
            }
        }

        // 8. Render
        draw_ui(force_x, force_y);
    }

    // Exit Logic
    cleanup_processes();
    return 0;
}
