/*
 * WATCHDOG PROCESS (pro_W)
 * ------------------------
 * Monitors the health of all active processes in the system.
 *
 * Mechanism:
 * 1. Reads 'registry.pid' to find active Process IDs (PIDs).
 * 2. Sends SIGUSR1 (Heartbeat Signal) to each PID.
 * 3. Expects processes to handle SIGUSR1 and log their status.
 * 4. Checks if processes are still running using kill(pid, 0).
 */

#include "common.h"
#include <signal.h>

#define CHECK_INTERVAL_SEC 2

int main(void) {
    register_process("Watchdog");

    // Log Header
    log_message(LOG_WATCHDOG, "--- WATCHDOG STARTED ---");

    while(1) {
        sleep(CHECK_INTERVAL_SEC);

        FILE *f = fopen(F_PID_REG, "r");
        if (!f) continue;

        char name[32];
        int pid;

        // Scan the Registry
        while (fscanf(f, "%s %d", name, &pid) == 2) {
            // Skip self to avoid killing the watchdog itself
            if (strcmp(name, "Watchdog") == 0) continue;

            // Check if process exists in the OS
            // kill(pid, 0) sends no actual signal but performs error checking.
            // If it returns 0, the process exists and we have permission to signal it.
            // If it returns -1 (ESRCH), the process is gone.
            if (kill(pid, 0) == 0) {
                // Determine if process is responsive (Liveness Check)
                // We send SIGUSR1 (User Defined Signal 1).
                // The target process is expected to catch this signal and write a 'HEARTBEAT'
                // entry to the 'watchdog.log' file.
                //
                // Note: A robust watchdog would verify that the log file was actually
                // updated with a fresh timestamp. For this assignment, simply sending
                // the signal and checking existence is sufficient to demonstrate the concept.

                kill(pid, SIGUSR1);
            } else {
                // CRITICAL FAILURE DETECTED
                // The process ID was in the registry but the OS says it's gone.
                // This implies a crash or unexpected termination.
                char msg[64];
                snprintf(msg, sizeof(msg), "ALERT: Process %s (PID %d) is DEAD/MISSING", name, pid);
                log_message(LOG_WATCHDOG, msg);

                // In a production system, we might attempt to restart the process.
                // Here, we log the failure.
            }
        }
        fclose(f);
    }
    return 0;
}
