#include "../include/common.h" 

#define COMM_PORT 5555
#define BUFFER_CAP 1024       // Increased buffer size
#define SYNC_RATE_US 30000 
#define RETRY_SEC 1

typedef struct {
    int conn_fd;
    int pipe_in_fd;  
    int pipe_out_fd; 
    int role; 
    
    // Internal Buffer State
    char net_buffer[BUFFER_CAP];
    int buf_start;
    int buf_end;
} LinkContext;

// --- ROBUST BUFFERED RECEIVER ---
// This function reads from the socket but handles fragmented packets 
// and multiple messages arriving at once.
int recv_line(LinkContext *ctx, char *dest, int max_len) {
    int line_idx = 0;
    
    while (1) {
        // 1. Check if we have data in the internal buffer
        while (ctx->buf_start < ctx->buf_end) {
            char c = ctx->net_buffer[ctx->buf_start++];
            
            if (c == '\n') {
                dest[line_idx] = '\0'; // Null-terminate
                
                // Logging
                char log_buf[BUFFER_CAP + 20];
                snprintf(log_buf, sizeof(log_buf), "RX << '%s'", dest);
                log_message(LOG_NETWORK, log_buf);
                
                return line_idx; // Successfully got a full line
            }
            
            if (line_idx < max_len - 1) {
                dest[line_idx++] = c;
            }
        }
        
        // 2. Buffer is empty or exhausted, read more from socket
        ctx->buf_start = 0;
        int n = read(ctx->conn_fd, ctx->net_buffer, BUFFER_CAP);
        
        if (n <= 0) {
            log_message(LOG_NETWORK, "DEBUG: Socket Closed or Connection Lost");
            return -1; // Error or Disconnect
        }
        
        ctx->buf_end = n;
    }
}

ssize_t send_line(int fd, const char *format, ...) {
    char payload[BUFFER_CAP];
    va_list args;
    va_start(args, format);
    vsnprintf(payload, sizeof(payload), format, args);
    va_end(args);

    // Logging
    char log_entry[BUFFER_CAP + 20];
    snprintf(log_entry, sizeof(log_entry), "TX >> '%s'", payload);
    log_message(LOG_NETWORK, log_entry);

    // Append Newline
    strncat(payload, "\n", BUFFER_CAP - strlen(payload) - 1);
    return write(fd, payload, strlen(payload));
}

int establish_link(int role, const char *target_ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in s_addr = {0};
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);

    char log_buf[128];
    snprintf(log_buf, 128, "--- SYSTEM: Connecting to %s:%d ---", target_ip, port);
    log_message(LOG_NETWORK, log_buf);

    if (role == MODE_SERVER) { 
        s_addr.sin_addr.s_addr = INADDR_ANY;
        int opt_val = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));
        
        if (bind(sockfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
            log_message(LOG_NETWORK, "FATAL: Bind Failed (Port busy?)");
            return -1;
        }
        listen(sockfd, 1);
        
        log_message(LOG_NETWORK, "--- SYSTEM: Waiting for Client... ---");
        int client_fd = accept(sockfd, NULL, NULL);
        log_message(LOG_NETWORK, "--- SYSTEM: Client Accepted! ---");
        close(sockfd);
        return client_fd;
    } else { 
        struct hostent *host = gethostbyname(target_ip);
        if (!host) {
            log_message(LOG_NETWORK, "FATAL: Invalid Host/IP");
            return -1;
        }
        memcpy(&s_addr.sin_addr, host->h_addr_list[0], host->h_length);
        
        while (connect(sockfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
            log_message(LOG_NETWORK, "--- SYSTEM: Retrying Connection... ---");
            sleep(RETRY_SEC);
        }
        log_message(LOG_NETWORK, "--- SYSTEM: Connected! ---");
        return sockfd;
    }
}

void execute_handshake(LinkContext *ctx) {
    char rx_buf[BUFFER_CAP];
    log_message(LOG_NETWORK, "--- PHASE: Handshake Start ---");
    
    if (ctx->role == MODE_SERVER) {
        // SERVER SIDE
        send_line(ctx->conn_fd, "ok");
        recv_line(ctx, rx_buf, BUFFER_CAP); // Expect "ook"
        
        send_line(ctx->conn_fd, "size %d %d", W_WIDTH, W_HEIGHT);
        recv_line(ctx, rx_buf, BUFFER_CAP); // Expect "sok"
    } else {
        // CLIENT SIDE
        recv_line(ctx, rx_buf, BUFFER_CAP); // Wait for "ok"
        send_line(ctx->conn_fd, "ook");
        
        recv_line(ctx, rx_buf, BUFFER_CAP); // Wait for "size ..."
        send_line(ctx->conn_fd, "sok");
    }
    log_message(LOG_NETWORK, "--- PHASE: Real-time Data ---");
}

void process_traffic(LinkContext *ctx) {
    DroneState local_drone = {0};
    DroneState remote_obs = {0}; 
    char buffer[BUFFER_CAP];
    float x_in, y_in;

    // Set Pipe to Non-Blocking so game doesn't freeze network
    fcntl(ctx->pipe_in_fd, F_SETFL, O_NONBLOCK);

    while (1) {
        // Drain local pipe to get latest position
        while (read(ctx->pipe_in_fd, &local_drone, sizeof(DroneState)) > 0);

        if (ctx->role == MODE_SERVER) {
            // SERVER LOOP
            send_line(ctx->conn_fd, "drone");
            send_line(ctx->conn_fd, "%.2f %.2f", local_drone.x, invert_axis(local_drone.y));
            recv_line(ctx, buffer, BUFFER_CAP); // Ack "dok"

            send_line(ctx->conn_fd, "obst");
            recv_line(ctx, buffer, BUFFER_CAP); // Obstacle Coords
            
            if(sscanf(buffer, "%f %f", &x_in, &y_in) == 2) {
                remote_obs.x = x_in;
                remote_obs.y = invert_axis(y_in);
                write(ctx->pipe_out_fd, &remote_obs, sizeof(DroneState));
            }
            send_line(ctx->conn_fd, "pok");

        } else {
            // CLIENT LOOP
            recv_line(ctx, buffer, BUFFER_CAP); // "drone" tag
            recv_line(ctx, buffer, BUFFER_CAP); // Coords
            
            if(sscanf(buffer, "%f %f", &x_in, &y_in) == 2) {
                remote_obs.x = x_in;
                remote_obs.y = invert_axis(y_in);
                write(ctx->pipe_out_fd, &remote_obs, sizeof(DroneState));
            }
            send_line(ctx->conn_fd, "dok");

            recv_line(ctx, buffer, BUFFER_CAP); // "obst" tag
            send_line(ctx->conn_fd, "%.2f %.2f", local_drone.x, invert_axis(local_drone.y));
            recv_line(ctx, buffer, BUFFER_CAP); // Ack "pok"
        }
        
        // Use select to handle socket closure gracefully or precise timing
        usleep(SYNC_RATE_US); 
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) return 1;
    register_process("Network");
    
    LinkContext ctx = {0};
    ctx.role = atoi(argv[1]);
    ctx.pipe_in_fd = atoi(argv[2]);
    ctx.pipe_out_fd = atoi(argv[3]);
    
    int port_num = (argc > 4) ? atoi(argv[4]) : COMM_PORT;
    char *ip_addr = (argc > 5) ? argv[5] : "127.0.0.1";

    ctx.conn_fd = establish_link(ctx.role, ip_addr, port_num);
    if (ctx.conn_fd < 0) return 1;

    execute_handshake(&ctx);
    process_traffic(&ctx);
    
    close(ctx.conn_fd);
    return 0;
}
