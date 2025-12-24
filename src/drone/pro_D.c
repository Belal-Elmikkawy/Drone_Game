#include "common.h"

// --- KEY VARIABLES FOR PHYSICS ---
// M: Mass of the drone (Inertia)
// K: Friction/Damping coefficient (Air resistance)
// T: Time step (Duration of one simulation cycle)
float M = DEFAULT_M;
float K = DEFAULT_K;
float T = DEFAULT_T; 

// --- KEY VARIABLES FOR POTENTIAL FIELDS ---
// ETA: Magnitude/Strength of the repulsive force
// RHO: The "Danger Radius" (Distance at which repulsion activates)
float ETA = DEFAULT_ETA;
float RHO = DEFAULT_RHO;

Point obstacles[MAX_OBSTACLES];
int current_w = DEFAULT_WIDTH;
int current_h = DEFAULT_HEIGHT;

float x_curr, y_curr, x_prev, y_prev;

// FUNCTION: load_params
// LOGIC: Reads 'params.txt' at runtime.
// REASON: Allows tuning physics (speed, friction) without recompiling.
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

// FUNCTION: parse_world_state
// LOGIC: Decodes the string sent by the Server (e.g., "W:100,30|F:1.0,0.0|O:...")
// REASON: Updates the local view of window size and User Command Forces.
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

// FUNCTION: calc_repulsion
// LOGIC: Implements the Artificial Potential Field method.
//        It calculates the distance to walls/obstacles. If dist < RHO, 
//        it applies a repulsive force inversely proportional to distance.
void calc_repulsion(float x, float y, float *rx, float *ry) {
    *rx = 0; *ry = 0;
    float dist;

    // ---  WALL REPULSION ---
    // The drone pushes away from the borders if it gets too close.
    
    // Left Wall
    dist = (x < 0.1f) ? 0.1f : x;
    if (dist < RHO) *rx += ETA * pow((1.0/dist - 1.0/RHO), 2);
    
    // Right Wall
    dist = current_w - x; 
    if (dist < 0.1f) dist = 0.1f;
    if (dist < RHO) *rx -= ETA * pow((1.0/dist - 1.0/RHO), 2);
    
    // Top Wall
    dist = (y < 0.1f) ? 0.1f : y;
    if (dist < RHO) *ry += ETA * pow((1.0/dist - 1.0/RHO), 2);
    
    // Bottom Wall
    dist = current_h - y; 
    if (dist < 0.1f) dist = 0.1f;
    if (dist < RHO) *ry -= ETA * pow((1.0/dist - 1.0/RHO), 2);

    // --- OBSTACLE REPULSION ---
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
    register_process("Drone");
    setup_watchdog_monitor("Drone");

    load_params();
    x_curr = DEFAULT_WIDTH / 2.0f; y_curr = DEFAULT_HEIGHT / 2.0f;
    x_prev = x_curr; y_prev = y_curr;

    float F_cmd_x = 0, F_cmd_y = 0;
    float F_rep_x = 0, F_rep_y = 0;
    int iter = 0;

    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    char buf[BUF_SIZE];
    struct timespec ts = {0, (long)(T * 1e9)};

    while (1) {
        set_status("Reading Input");
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = 0;
            char *start = strrchr(buf, 'W');
            if (!start) start = strrchr(buf, 'F');
            if (start) parse_world_state(start, &F_cmd_x, &F_cmd_y);
        }

        if (++iter % 20 == 0) {
            set_status("Reloading Params");
            load_params();
        }

        set_status("Physics Calculation");
        calc_repulsion(x_curr, y_curr, &F_rep_x, &F_rep_y);
        
        // --- ASSIGNMENT 1 FIX: MANUAL CONTROL ONLY ---
        // Originally, there was an "Attraction Force" pulling the drone to targets.
        // That was REMOVED to ensure the drone only moves when buttons are pressed (F_cmd)
        // or when pushed by walls (F_rep).
        float F_total_x = F_cmd_x + F_rep_x;
        float F_total_y = F_cmd_y + F_rep_y;
        
        // --- PHYSICS ENGINE (Euler Integration) ---
        // Calculates the next position based on Force, Mass, and Friction.
        float a = M / (T * T);
        float b = K / T;
        float next_x = (F_total_x + (2 * a + b) * x_curr - a * x_prev) / (a + b);
        float next_y = (F_total_y + (2 * a + b) * y_curr - a * y_prev) / (a + b);

        x_prev = x_curr; y_prev = y_curr;
        x_curr = next_x; y_curr = next_y;

        // Geo-fencing (Hard limits to keep drone in bounds)
        if (x_curr < 1.0f) { x_curr = 1.0f; x_prev = 1.0f; }
        if (x_curr > current_w - 1.0f) { x_curr = current_w - 1.0f; x_prev = current_w - 1.0f; }
        if (y_curr < 1.0f) { y_curr = 1.0f; y_prev = 1.0f; }
        if (y_curr > current_h - 1.0f) { y_curr = current_h - 1.0f; y_prev = current_h - 1.0f; }

        printf("%.2f,%.2f\n", x_curr, y_curr);
        fflush(stdout);
        
        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf), "POS:(%.2f,%.2f) CMD:(%.1f,%.1f)", x_curr, y_curr, F_cmd_x, F_cmd_y);
        log_message(LOG_DRONE, log_buf);

        set_status("Sleeping");
        nanosleep(&ts, NULL);
    }
    return 0;
}
