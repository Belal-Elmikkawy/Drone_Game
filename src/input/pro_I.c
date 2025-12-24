#include "common.h"
#include <termios.h>

// FUNCTION: set_raw_mode
// LOGIC: Disables 'canonical mode' (buffering) and 'echo'.
// REASON: Allows the game to read keypresses instantly without the user pressing ENTER.
void set_raw_mode(int enable) {
    static struct termios oldt, newt;
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        newt.c_cc[VMIN] = 1; newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

int main() {
    register_process("Input");
    setup_watchdog_monitor("Input");

    set_raw_mode(1);
    
    float Fx = 0.0f; 
    float Fy = 0.0f;
    char c;
    
    tcflush(STDIN_FILENO, TCIFLUSH);
    printf("0.0,0.0\n"); fflush(stdout);

    while (1) {
        set_status("Waiting Keypress");
        if (read(STDIN_FILENO, &c, 1) > 0) {
            set_status("Processing Key");
            switch(c) {
                // --- ASSIGNMENT 1 FIX: BUTTON INTERFERENCE ---
                // Problem: If user pressed UP then LEFT, forces would accumulate (Fx=-1, Fy=-1)
                //          causing diagonal drift when not intended.
                // Fix: When a vertical key is pressed, we explicitly reset Fx to 0.0.
                //      When a horizontal key is pressed, we explicitly reset Fy to 0.0.
                
                // UP (Reset Horizontal Force)
                case 'e': case 'i': 
                    Fy -= 1.0f; 
                    Fx = 0.0f; 
                    break; 

                // DOWN (Reset Horizontal Force)
                case 'c': case ',': 
                    Fy += 1.0f; 
                    Fx = 0.0f; 
                    break; 

                case 'x': 
                    Fy += 1.0f; 
                    Fx = 0.0f; 
                    break;

                // LEFT (Reset Vertical Force)
                case 's': case 'j': 
                    Fx -= 1.0f; 
                    Fy = 0.0f; 
                    break; 

                // RIGHT (Reset Vertical Force)
                case 'f': case 'l': 
                    Fx += 1.0f; 
                    Fy = 0.0f; 
                    break; 

                // --- DIAGONALS ---
                // We keep specific keys for diagonal movement if the user intentionally wants it.
                case 'w': Fx -= 1.0f; Fy -= 1.0f; break; // Up-Left
                case 'r': Fx += 1.0f; Fy -= 1.0f; break; // Up-Right
                case 'v': Fx += 1.0f; Fy += 1.0f; break; // Down-Right

                // BRAKE / STOP (Reset All Forces)
                case 'd': case 'k': case ' ': 
                    Fx = 0.0f; Fy = 0.0f; 
                    break;

                case 'q': 
                    set_raw_mode(0);
                    exit(0);
            }
            
            // Limit Maximum Force to keep physics stable
            if (Fx > 10.0f) Fx = 10.0f; if (Fx < -10.0f) Fx = -10.0f;
            if (Fy > 10.0f) Fy = 10.0f; if (Fy < -10.0f) Fy = -10.0f;

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
