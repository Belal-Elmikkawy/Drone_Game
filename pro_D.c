// pro_D.c
#include "common.h"

float M = DEFAULT_M, K = DEFAULT_K, T = DEFAULT_T;
float ETA = DEFAULT_ETA, RHO = DEFAULT_RHO;

Point obstacles[MAX_OBSTACLES];
Point targets[MAX_TARGETS];
int current_w = DEFAULT_WIDTH;
int current_h = DEFAULT_HEIGHT;

void log_physics(float x, float y, float fx, float fy, float rx, float ry, float ax, float ay) {
    FILE *f = fopen(LOG_DRONE, "a");
    if(f) {
        fprintf(f, "POS:(%.1f,%.1f) | CMD:(%.1f,%.1f) | REPUL:(%.1f,%.1f) | ATTR:(%.1f,%.1f)\n", 
                x, y, fx, fy, rx, ry, ax, ay);
        fclose(f);
    }
}

void load_params() {
    FILE *f = fopen("params.txt", "r");
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "M=", 2) == 0) M = atof(line+2);
            if (strncmp(line, "K=", 2) == 0) K = atof(line+2);
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

    memset(targets, 0, sizeof(targets));
    char *t_ptr = strstr(buf, "T:");
    if(t_ptr) {
        t_ptr += 2;
        int i=0, tx, ty, off;
        while(sscanf(t_ptr, "%d,%d%n", &tx, &ty, &off) == 2 && i < MAX_TARGETS) {
            targets[i].x = tx; targets[i].y = ty;
            i++; t_ptr += off; if(*t_ptr == ';') t_ptr++; else break;
        }
    }
}

void calc_forces(float x, float y, float *rx, float *ry, float *ax, float *ay) {
    *rx = 0; *ry = 0; *ax = 0; *ay = 0;
    float dist;

    // Wall Repulsion
    dist = (x < 0.1f) ? 0.1f : x;
    if (dist < RHO) *rx += ETA * pow((1.0/dist - 1.0/RHO), 2);
    dist = current_w - x; if (dist <= 0.1f) dist = 0.1f;
    if (dist < RHO) *rx -= ETA * pow((1.0/dist - 1.0/RHO), 2);
    
    dist = (y < 0.1f) ? 0.1f : y;
    if (dist < RHO) *ry += ETA * pow((1.0/dist - 1.0/RHO), 2);
    dist = current_h - y; if (dist <= 0.1f) dist = 0.1f;
    if (dist < RHO) *ry -= ETA * pow((1.0/dist - 1.0f/RHO), 2);

    // Obstacle Repulsion
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

    // UPDATED: Target Attraction (Constant pull to NEAREST target)
    int nearest_idx = -1;
    float min_dist = 100000.0f;

    for(int i=0; i<MAX_TARGETS; i++) {
        if(targets[i].x == 0) continue;
        float dx = targets[i].x - x;
        float dy = targets[i].y - y;
        float d = sqrt(dx*dx + dy*dy);
        
        if (d < min_dist) {
            min_dist = d;
            nearest_idx = i;
        }
    }

    if (nearest_idx != -1) {
        float dx = targets[nearest_idx].x - x;
        float dy = targets[nearest_idx].y - y;
        float d = min_dist < 0.1f ? 0.1f : min_dist;
        
        // Constant force strength (tune this if too fast/slow)
        float strength = 2.0f; 
        
        // Normalized vector * strength
        *ax += (dx / d) * strength;
        *ay += (dy / d) * strength;
    }
}

int main(void) {
    load_params();
    float x_prev2 = DEFAULT_WIDTH/2.0f, y_prev2 = DEFAULT_HEIGHT/2.0f;
    float x_prev = DEFAULT_WIDTH/2.0f, y_prev = DEFAULT_HEIGHT/2.0f;
    float F_cmd_x = 0, F_cmd_y = 0;

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

        float F_rep_x, F_rep_y, F_att_x, F_att_y;
        calc_forces(x_prev, y_prev, &F_rep_x, &F_rep_y, &F_att_x, &F_att_y);

        float F_tot_x = F_cmd_x + F_rep_x + F_att_x;
        float F_tot_y = F_cmd_y + F_rep_y + F_att_y;

        float term1 = M + K*T;
        float term2 = 2*M + K*T;
        float x_next = ( (T*T*F_tot_x) + (term2*x_prev) - (M*x_prev2) ) / term1;
        float y_next = ( (T*T*F_tot_y) + (term2*y_prev) - (M*y_prev2) ) / term1;

        x_prev2 = x_prev; y_prev2 = y_prev;
        x_prev = x_next; y_prev = y_next;
        
        if (x_prev < 1.0f) { x_prev = 1.0f; x_prev2 = 1.0f; }
        if (x_prev > current_w-1.0f) { x_prev = current_w-1.0f; x_prev2 = current_w-1.0f; }
        if (y_prev < 1.0f) { y_prev = 1.0f; y_prev2 = 1.0f; }
        if (y_prev > current_h-1.0f) { y_prev = current_h-1.0f; y_prev2 = current_h-1.0f; }

        printf("%.2f,%.2f\n", x_prev, y_prev);
        fflush(stdout);
        
        // Log to file for the Dashboard
        log_physics(x_prev, y_prev, F_cmd_x, F_cmd_y, F_rep_x, F_rep_y, F_att_x, F_att_y);
        
        nanosleep(&ts, NULL);
    }
    return 0;
}
