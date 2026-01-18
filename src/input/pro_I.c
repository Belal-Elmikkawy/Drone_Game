/*
 * INPUT PROCESS (pro_I)
 * ---------------------
 * Handles User Interface Input (Keyboard).
 * 1. Puts terminal into 'Raw Mode' to capture keypresses instantly without Enter.
 * 2. Maps keys (WASD, IJKL, etc.) to Force Vectors.
 * 3. Sends calculated Force to the Server via Pipe.
 */

#include "common.h"
#include <termios.h>

/*
 * Configures the terminal to standard "Raw" mode.
 * Disables canonical line buffering and echo.
 */
void set_raw_mode(int enable) {
    static struct termios oldt, newt;
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // Disable buffer & echo
        newt.c_cc[VMIN] = 1; // Read at least 1 char
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Restore settings
    }
}

int main() {
    register_process("Input");
    setup_watchdog_monitor("Input");

    set_raw_mode(1); // Enable Raw Mode

    float Fx = 0.0f;
    float Fy = 0.0f;
    char c;

    tcflush(STDIN_FILENO, TCIFLUSH);
    printf("0.0,0.0\n"); fflush(stdout); // Initial zero force

    while (1) {
        set_status("Waiting Keypress");

        // Blocking Read (Raw mode returns immediately on keypress)
        if (read(STDIN_FILENO, &c, 1) > 0) {
            set_status("Processing Key");
            switch(c) {
                // --- ARROW-STYLE CONTROLS ---
                // UP
                case 'e': case 'i':
                    Fy -= 1.0f; Fx = 0.0f; break;
                // DOWN
                case 'c': case ',':
                    Fy += 1.0f; Fx = 0.0f; break;
                // LEFT
                case 's': case 'j':
                    Fx -= 1.0f; Fy = 0.0f; break;
                // RIGHT
                case 'f': case 'l':
                    Fx += 1.0f; Fy = 0.0f; break;

                // --- DIAGONALS ---
                case 'w': Fx -= 1.0f; Fy -= 1.0f; break; // Up-Left
                case 'r': Fx += 1.0f; Fy -= 1.0f; break; // Up-Right
                case 'v': Fx += 1.0f; Fy += 1.0f; break; // Down-Right
                case 'x': Fx -= 1.0f; Fy += 1.0f; break; // Down-Left

                // --- UTILITY ---
                case 'd': case 'k': case ' ':
                    Fx = 0.0f; Fy = 0.0f; break; // BRAKE (Stop)

                case 'q':
                    set_raw_mode(0); exit(0); // QUIT
            }

            // Limit Maximum Force
            if (Fx > 10.0f) Fx = 10.0f; if (Fx < -10.0f) Fx = -10.0f;
            if (Fy > 10.0f) Fy = 10.0f; if (Fy < -10.0f) Fy = -10.0f;

            // Send to Server
            printf("%.2f,%.2f\n", Fx, Fy);
            fflush(stdout);

            char msg[64];
            snprintf(msg, sizeof(msg), "Key: %c Force: (%.1f, %.1f)", c, Fx, Fy);
            log_message(LOG_INPUT, msg);
        }
    }
    set_raw_mode(0);
    return 0;
}
