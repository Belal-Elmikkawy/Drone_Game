/*
 * WATCHDOG PROCESS (pro_W)
 * ------------------------
 * Monitors system health by checking process responsiveness.
 * 1. Reads registered PIDs from `pid_registry.txt`.
 * 2. Sends SIGUSR1 signal to each process.
 * 3. Expects the process to have a signal handler that functions correctly.
 * 4. If a process does not exist or fails (kill returns -1), triggers system shutdown.
 */

#include "common.h"
#include <ncurses.h>

typedef struct {
    char name[32];
    pid_t pid;
} MonitoredProcess;

MonitoredProcess processes[10];
int proc_count = 0;
pid_t server_pid = -1;

/* Reads the registry file to find active processes */
void load_pids() {
    proc_count = 0;
    server_pid = -1;

    FILE *f = fopen(FILE_PID, "r");
    if (!f) return;

    int fd = fileno(f);
    flock(fd, LOCK_SH); // Shared Lock for thread safety

    char name[32];
    int pid;
    while(fscanf(f, "%s %d", name, &pid) == 2) {
        strcpy(processes[proc_count].name, name);
        processes[proc_count].pid = pid;
        if(strcmp(name, "Server") == 0) server_pid = pid;
        proc_count++;
        if(proc_count >= 10) break;
    }
    flock(fd, LOCK_UN);
    fclose(f);
}

int main() {
    initscr(); cbreak(); noecho(); curs_set(0);

    FILE *f = fopen(LOG_WATCHDOG, "w"); if(f) fclose(f);

    register_process("Watchdog"); // Register self too

    mvprintw(0, 0, "--- WATCHDOG MONITOR ---");
    refresh();

    while(1) {
        load_pids();

        erase();
        mvprintw(0, 0, "--- WATCHDOG MONITOR ---");
        mvprintw(1, 0, "Monitoring %d processes...", proc_count);
        mvprintw(2, 0, "--------------------------------");

        for(int i=0; i<proc_count; i++) {
            // Ping process with Signal 0 (Existence Check) or SIGUSR1 (Heartbeat Request)
            // Here we use SIGUSR1. If process is hung/zombie, it might fail or not respond conceptually.
            // Using kill(pid, 0) checks if process merely exists.
            int res = kill(processes[i].pid, SIGUSR1);

            if (res == 0) {
                mvprintw(3+i, 2, "[%s] (PID %d): ACTIVE", processes[i].name, processes[i].pid);
            } else {
                // FAILURE DETECTED
                mvprintw(3+i, 2, "[%s] (PID %d): UNRESPONSIVE! STOPPING SYSTEM...", processes[i].name, processes[i].pid);
                refresh();

                log_message(LOG_WATCHDOG, "ALERT: Process Unresponsive. Killing Server to stop system.");

                // Trigger Cascade Shutdown via Server, or kill specific if Server is dead
                if(server_pid > 0) kill(server_pid, SIGKILL);
                else kill(processes[i].pid, SIGKILL);

                sleep(2);
                endwin();
                exit(1);
            }
        }

        mvprintw(15, 0, "Logs are written to %s", LOG_WATCHDOG);
        refresh();
        sleep(2); // Check every 2 seconds
    }

    endwin();
    return 0;
}
