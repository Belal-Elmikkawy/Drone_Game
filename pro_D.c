// pro_D.c - Drone Physics
#include "common.h"

float T = DEFAULT_T; 
float ETA = DEFAULT_ETA, RHO = DEFAULT_RHO;

Point obstacles[MAX_OBSTACLES];
int current_w = DEFAULT_WIDTH;
int current_h = DEFAULT_HEIGHT;
int motion_started = 0; 

void log_physics(float x, float y, float fx, float fy, float rx, float ry) {
    FILE *f = fopen(LOG_DRONE, "a");
    if(f) {
        fprintf(f, "POS:(%.1f,%.1f) | CMD:(%.1f,%.1f) | REPUL:(%.1f,%.1f)\n", 
                x, y, fx, fy, rx, ry);
        fclose(f);
    }
}

void load_params() {
    FILE *f = fopen("params.txt", "r");
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "T=", 2) == 0) T = atof(line+2);
            if (strncmp(line, "ETA=", 4) == 0) ETA = atof(line+4);
            if (strncmp(line, "RHO=", 4) == 0) RHO = atof(line+4);
        }
        fclose(f);
    }
}

void parse_world_state(char *buf, float *fx, float *fy) {
    char *w_ptr = strstr(buf, "W:");
    if(w_ptr) sscanf(w_ptr, "W:%d,%d", &current_w, &current_h);

    char *f_ptr = strstr(buf, "F:");
    if(f_ptr) sscanf(f_ptr, "F:%f,%f", fx, fy);
    
    memset(obstacles, 0, sizeof(obstacles));
    char *o_ptr = strstr(buf, "O:");
    if(o_ptr) {
        o_ptr += 2;
        int i=0, ox, oy, off;
        while(sscanf(o_ptr, "%d,%d%n", &ox, &oy, &off) == 2 && i < MAX_OBSTACLES) {
            obstacles[i].x = ox; obstacles[i].y = oy;
            i++; o_ptr += off; if(*o_ptr == ';') o_ptr++; else break;
        }
    }
}

void calc_repulsion(float x, float y, float *rx, float *ry) {
    *rx = 0; *ry = 0;
    float dist;

    dist = (x < 0.1f) ? 0.1f : x;
    if (dist < RHO) *rx += ETA * pow((1.0/dist - 1.0/RHO), 2);
    dist = current_w - x; if (dist <= 0.1f) dist = 0.1f;
    if (dist < RHO) *rx -= ETA * pow((1.0/dist - 1.0/RHO), 2);
    
    dist = (y < 0.1f) ? 0.1f : y;
    if (dist < RHO) *ry += ETA * pow((1.0/dist - 1.0/RHO), 2);
    dist = current_h - y; if (dist <= 0.1f) dist = 0.1f;
    if (dist < RHO) *ry -= ETA * pow((1.0/dist - 1.0f/RHO), 2);

    for(int i=0; i<MAX_OBSTACLES; i++) {
        if(obstacles[i].x == 0) continue;
        float dx = x - obstacles[i].x;
        float dy = y - obstacles[i].y;
        dist = sqrt(dx*dx + dy*dy);
        if(dist < 0.1f) dist = 0.1f;
        if(dist < RHO) {
            float mag = ETA * pow((1.0/dist - 1.0/RHO), 2);
            *rx += mag * (dx/dist); *ry += mag * (dy/dist);
        }
    }
}

int main(void) {
    load_params();
    float x_curr = DEFAULT_WIDTH/2.0f; 
    float y_curr = DEFAULT_HEIGHT/2.0f;
    float F_cmd_x = 0, F_cmd_y = 0;

    // --- SPEED CONTROL ---
    // Kept at 0.1f for slow, controlled movement.
    float SPEED_SCALAR = 0.1f; 

    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    char buf[BUF_SIZE];
    struct timespec ts = {0, (long)(T * 1e9)};

    while (1) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = 0;
            char *start = strrchr(buf, 'W');
            if (!start) start = strrchr(buf, 'F');
            if (start) parse_world_state(start, &F_cmd_x, &F_cmd_y);
        }

        if (!motion_started) {
            if (fabs(F_cmd_x) > 0.1f || fabs(F_cmd_y) > 0.1f) motion_started = 1;
        }

        float F_rep_x = 0, F_rep_y = 0;
        
        if (motion_started) {
            calc_repulsion(x_curr, y_curr, &F_rep_x, &F_rep_y);

            float dir_x = 0;
            if (F_cmd_x > 0.1f) dir_x = 1.0f;
            else if (F_cmd_x < -0.1f) dir_x = -1.0f;

            float dir_y = 0;
            if (F_cmd_y > 0.1f) dir_y = 1.0f;
            else if (F_cmd_y < -0.1f) dir_y = -1.0f;

            // --- DIAGONAL CORRECTION ---
            if (dir_x != 0 && dir_y != 0) {
                dir_x *= 0.7071f; 
                dir_y *= 0.7071f;
            }

            float move_x = (dir_x + F_rep_x * 0.1f) * SPEED_SCALAR;
            float move_y = (dir_y + F_rep_y * 0.1f) * SPEED_SCALAR;

            x_curr += move_x;
            y_curr += move_y;

            if (x_curr < 1.0f) x_curr = 1.0f;
            if (x_curr > current_w-1.0f) x_curr = current_w-1.0f;
            if (y_curr < 1.0f) y_curr = 1.0f;
            if (y_curr > current_h-1.0f) y_curr = current_h-1.0f;
        }

        printf("%.2f,%.2f\n", x_curr, y_curr);
        fflush(stdout);
        
        log_physics(x_curr, y_curr, F_cmd_x, F_cmd_y, F_rep_x, F_rep_y);
        nanosleep(&ts, NULL);
    }
    return 0;
}
