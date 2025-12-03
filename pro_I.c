#include "common.h"
#include <termios.h>

void log_key(char key, float fx, float fy) {
    FILE *f = fopen(LOG_INPUT, "a");
    if(f) {
        // Human readable log for the Dashboard
        fprintf(f, "> Key: [%c] | Force Vector: (%.1f, %.1f)\n", key, fx, fy);
        fclose(f);
    }
}

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
    set_raw_mode(1);
    float Fx = 0.0f, Fy = 0.0f;
    const float STEP = 1.0f;
    char c;
    
    tcflush(STDIN_FILENO, TCIFLUSH);
    printf("0.0,0.0\n"); fflush(stdout);

    while (read(STDIN_FILENO, &c, 1) > 0) {
        switch(c) {
            case 'e': case 'i': Fy -= STEP; break; 
            case 'c': case ',': Fy += STEP; break; 
            case 's': case 'j': Fx -= STEP; break; 
            case 'f': case 'l': Fx += STEP; break; 
            case 'w': Fx -= STEP; Fy -= STEP; break;
            case 'r': Fx += STEP; Fy -= STEP; break;
            case 'x': Fx -= STEP; Fy += STEP; break;
            case 'v': Fx += STEP; Fy += STEP; break;
            case 'd': case 'k': case ' ': Fx = 0.0f; Fy = 0.0f; break;
            case 'q': exit(0);
        }
        printf("%.2f,%.2f\n", Fx, Fy);
        fflush(stdout);
        
        // Log to file for the Dashboard
        log_key(c, Fx, Fy);
    }
    set_raw_mode(0);
    return 0;
}
