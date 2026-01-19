/*
 * INPUT PROCESS (pro_I)
 * ---------------------
 * Captures user keyboard input and converts it into control forces.
 *
 * Mechanism:
 * 1. Sets terminal to RAW MODE (disables buffering/echo).
 * 2. Reads key presses (WASD / Arrows).
 * 3. Maps keys to Forces (Newtonian logic).
 * 4. Sends Force Vector (Fx, Fy) to Server.
 */

#include "common.h"
#include <termios.h>

// --- INPUT SETTINGS ---
#define INPUT_STEP 1.0f  // Force increment per keypress
#define DECAY_RATE 0.9f  // Force decay when no key is pressed (simulates spring return)

/*
 * set_conio_terminal_mode
 * -----------------------
 * Configures the terminal for real-time game input.
 * - Disables Canonical Mode: Input is available immediately, not after 'Enter'.
 * - Disables Echo: Typed keys are not shown on screen.
 *
 * This allows 'getch()' style immediate reading which is essential for smooth controls.
 */
void set_conio_terminal_mode() {
    struct termios new_termios;
    tcgetattr(0, &new_termios);
    new_termios.c_lflag &= ~ICANON; // Disable line buffering
    new_termios.c_lflag &= ~ECHO;   // Disable echo
    tcsetattr(0, TCSANOW, &new_termios);
}

/*
 * kbhit
 * -----
 * Checks if a keyboard key has been pressed.
 * Returns: 1 if key waiting, 0 otherwise.
 * Logic: Uses select() on stdin (fd 0) with 0 timeout.
 */
int kbhit() {
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

/*
 * getch
 * -----
 * Reads a single character from stdin.
 */
int getch() {
    int r;
    unsigned char c;
    if ((r = read(0, &c, sizeof(c))) < 0) return r;
    else return c;
}

int main(void) {
    // 1. Startup
    register_process("Input");
    setup_watchdog_monitor("Input");

    set_conio_terminal_mode();

    float Fx = 0.0f;
    float Fy = 0.0f;
    int key = 0;

    // Polling Interval
    struct timespec ts = {0, 50000000}; // 50ms

    while (1) {
        set_status("Polling Input");

        // 2. Read Keys
        if (kbhit()) {
            key = getch();

            // Handle Arrow Keys (Sequence: ESC -> [ -> A/B/C/D)
            if (key == 27) {
                getch(); // Skip '['
                key = getch();
                switch(key) {
                    case 'A': key = 'e'; break; // Up
                    case 'B': key = 'x'; break; // Down
                    case 'C': key = 'f'; break; // Right
                    case 'D': key = 's'; break; // Left
                }
            }

            // Map Keys to Forces
            // E/I/8 = Up | X/M/2 = Down | S/J/4 = Left | F/L/6 = Right
            key = tolower(key);
            if (key == 'e' || key == 'i' || key == '8') Fy -= INPUT_STEP;
            else if (key == 'x' || key == 'm' || key == '2') Fy += INPUT_STEP;
            else if (key == 's' || key == 'j' || key == '4') Fx -= INPUT_STEP;
            else if (key == 'f' || key == 'l' || key == '6') Fx += INPUT_STEP;
            else if (key == 'r') { Fy = 0; Fx = 0; } // Brake/Reset
            else if (key == ' ') { Fy = 0; Fx = 0; } // Space Brake
            else if (key == 'q') { break; } // Quit
        } else {
            // Decay forces slightly if no input (Damping)
            // Fx *= DECAY_RATE;
            // Fy *= DECAY_RATE;
            // (Commented out to allow "Cruise Control" feel, uncomment for specific control style)
        }

        // 3. Clamp Forces
        if (Fx > 10.0f) Fx = 10.0f;
        if (Fx < -10.0f) Fx = -10.0f;
        if (Fy > 10.0f) Fy = 10.0f;
        if (Fy < -10.0f) Fy = -10.0f;

        // 4. Send to Server (via Pipe)
        printf("%.2f,%.2f\n", Fx, Fy);
        fflush(stdout);

        // Debug Log
        char log_msg[64];
        snprintf(log_msg, sizeof(log_msg), "Key: %c | Force: (%.1f, %.1f)", key, Fx, Fy);
        log_message(LOG_INPUT, log_msg);

        nanosleep(&ts, NULL);
    }

    return 0;
}
