#include "common.h"
#include <ncurses.h>

typedef struct {
    char name[32];
    pid_t pid;
} MonitoredProcess;

MonitoredProcess processes[10];
int proc_count = 0;

void load_pids() {
    proc_count = 0;
    FILE *f = fopen(FILE_PID, "r");
    if (!f) return;
    
    int fd = fileno(f);
    flock(fd, LOCK_SH); // Read Lock

    char name[32];
    int pid;
    while(fscanf(f, "%s %d", name, &pid) == 2) {
        strcpy(processes[proc_count].name, name);
        processes[proc_count].pid = pid;
        proc_count++;
        if(proc_count >= 10) break;
    }
    
    flock(fd, LOCK_UN);
    fclose(f);
}

int main() {
    initscr(); cbreak(); noecho(); curs_set(0);
    
    // Clear old logs
    FILE *f = fopen(LOG_WATCHDOG, "w"); if(f) fclose(f);
    
    register_process("Watchdog");

    mvprintw(0, 0, "--- WATCHDOG MONITOR ---");
    refresh();

    while(1) {
        load_pids();
        
        erase();
        mvprintw(0, 0, "--- WATCHDOG MONITOR ---");
        mvprintw(1, 0, "Monitoring %d processes...", proc_count);
        mvprintw(2, 0, "--------------------------------");
        
        for(int i=0; i<proc_count; i++) {
            // Poll process by sending SIGUSR1
            // The process will respond by writing to the log file
            int res = kill(processes[i].pid, SIGUSR1);
            
            if (res == 0) {
                mvprintw(3+i, 2, "[%s] (PID %d): ACTIVE", processes[i].name, processes[i].pid);
            } else {
                mvprintw(3+i, 2, "[%s] (PID %d): UNRESPONSIVE!", processes[i].name, processes[i].pid);
                log_message(LOG_WATCHDOG, "ALERT: Process Unresponsive");
            }
        }
        
        mvprintw(15, 0, "Logs are written to %s", LOG_WATCHDOG);
        refresh();
        
        sleep(2); // Cycle T [cite: 129]
    }

    endwin();
    return 0;
}
