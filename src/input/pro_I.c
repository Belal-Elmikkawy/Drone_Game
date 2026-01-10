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
    // --- [ASSIGNMENT 2 CORRECTION] ---
    // Registration with the Watchdog system is required for Assignment 2.
    // This allows pro_W to monitor if this process is alive/responsive.
    register_process("Input");
    setup_watchdog_monitor("Input"); 

    set_raw_mode(1);
    
    float Fx = 0.0f; 
    float Fy = 0.0f;
    char c;
    
    tcflush(STDIN_FILENO, TCIFLUSH);
    printf("0.0,0.0\n"); fflush(stdout);

    while (1) {
        // --- [ASSIGNMENT 2 CORRECTION] ---
        // Updates status for the Watchdog (visible in the log file)
        set_status("Waiting Keypress");
        
        if (read(STDIN_FILENO, &c, 1) > 0) {
            set_status("Processing Key");
            switch(c) {
                // --- CARDINAL DIRECTIONS ---
                
                // UP (Reset Horizontal Force)
                case 'e': case 'i': 
                    Fy -= 1.0f; 
                    Fx = 0.0f; 
                    break; 

                // DOWN (Reset Horizontal Force)
                // 'c' is central-bottom for left hand, ',' is for right hand
                case 'c': case ',': 
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

                // --- DIAGONAL DIRECTIONS ---
                
                // Up-Left (Left hand: 'w', Right hand: 'u')
                case 'w': case 'u':
                    Fx -= 1.0f; 
                    Fy -= 1.0f; 
                    break; 
                
                // Up-Right (Left hand: 'r', Right hand: 'o')
                case 'r': case 'o':
                    Fx += 1.0f; 
                    Fy -= 1.0f; 
                    break; 
                
                // Down-Left (Left hand: 'x', Right hand: 'm')
                // [BUG FIX] Previously 'x' was duplicated as DOWN. Fixed to Diagonal.
                case 'x': case 'm':
                    Fx -= 1.0f; 
                    Fy += 1.0f; 
                    break;

                // Down-Right (Left hand: 'v', Right hand: '.')
                case 'v': case '.':
                    Fx += 1.0f; 
                    Fy += 1.0f; 
                    break; 

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

            // --- [ASSIGNMENT 2 CORRECTION] ---
            // Logging input events to a specific log file (pro_I responsibility)
            char msg[64];
            snprintf(msg, sizeof(msg), "Key: %c Force: (%.1f, %.1f)", c, Fx, Fy);
            log_message(LOG_INPUT, msg);
        }
    }
    set_raw_mode(0);
    return 0;
}
