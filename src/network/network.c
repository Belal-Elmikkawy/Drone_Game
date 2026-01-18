#include "../include/common.h"
#include <netinet/tcp.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#define COMM_PORT 5555
#define BUFFER_CAP 2048
#define RETRY_SEC 1

// Dynamic World State
static int DYNAMIC_WIDTH = W_WIDTH;
static int DYNAMIC_HEIGHT = W_HEIGHT;

static inline float dynamic_invert_axis(float val) {
    return (float)(DYNAMIC_HEIGHT - 1) - val;
}

// Log ONLY to File (silence stderr to avoid game UI pollution)
void log_to_file(const char *format, ...) {
    char entry[1024];
    va_list args;

    // Format message
    va_start(args, format);
    vsnprintf(entry, sizeof(entry), format, args);
    va_end(args);

    // Append to Log File
    FILE *f = fopen("network.log", "a");
    if (f) {
        // Timestamp
        time_t now = time(NULL);
        char *t_str = ctime(&now);
        t_str[strlen(t_str)-1] = 0; // trim newline
        fprintf(f, "[%s] %s\n", t_str, entry);
        fclose(f);
    }
}

// compatibility mapping
#define log_message(type, msg) log_to_file("%s", msg)

typedef struct {
    int conn_fd;
    int pipe_in_fd;
    int pipe_out_fd;
    int role;

    char net_buffer[BUFFER_CAP];
    int buf_start;
    int buf_end;
    int connected; // Flag to track connection state
} LinkContext;

// --- ROBUST BUFFERED RECEIVER ---
// Returns length of line found, 0 if nothing yet, -1 on error/EOF
int recv_line_buffer(LinkContext *ctx, char *dest, int max_len) {
    int line_idx = 0;

    while (1) {
        // Drain Internal Buffer
        while (ctx->buf_start < ctx->buf_end) {
            char c = ctx->net_buffer[ctx->buf_start++];
            if (c == '\n') {
                dest[line_idx] = '\0';
                return line_idx;
            }
            if (line_idx < max_len - 1) dest[line_idx++] = c;
        }

        return 0; // Need more data
    }
}

// Returns 1 if data read, 0 if nothing (EAGAIN), -1 if EOF/Error
int fill_buffer(LinkContext *ctx) {
    // Only read if buffer is empty or exhausted
    if (ctx->buf_start >= ctx->buf_end) {
        ctx->buf_start = 0;
        int n = read(ctx->conn_fd, ctx->net_buffer, BUFFER_CAP);
        if (n > 0) {
            ctx->buf_end = n;
            return 1;
        } else if (n == 0) {
            return -1; // EOF
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_to_file("ERROR: Socket Error: %d", errno);
            return -1; // Error
        } else {
            return 0; // EAGAIN
        }
    }
    return 1; // Buffer has data
}

ssize_t send_line(int fd, const char *format, ...) {
    char payload[BUFFER_CAP];
    va_list args;
    va_start(args, format);
    vsnprintf(payload, sizeof(payload), format, args);
    va_end(args);

    if(strstr(payload, "drone") == NULL && strstr(payload, "obst") == NULL)
         log_to_file("TX >> '%s'", payload);

    strncat(payload, "\n", BUFFER_CAP - strlen(payload) - 1);
    return write(fd, payload, strlen(payload));
}

int establish_link(int role, const char *target_ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    // Timers - ONLY for connected socket, not listener
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    struct sockaddr_in s_addr = {0};
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);

    log_to_file("--- SYSTEM: Role %d | Port %d ---", role, port);

    if (role == MODE_SERVER) {
        s_addr.sin_addr.s_addr = INADDR_ANY;
        int opt_val = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

        // NO TIMEOUT on Listening Socket (allow infinite wait for client)

        if (bind(sockfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
            log_to_file("FATAL: Bind Failed");
            return -1;
        }
        listen(sockfd, 1);

        log_to_file("--- SYSTEM: Listening on Port %d... Waiting for Client... ---", port);
        int client_fd = accept(sockfd, NULL, NULL);
        if (client_fd < 0) {
             log_to_file("FATAL: Accept Failed (errno %d)", errno);
             return -1;
        }

        log_to_file("--- SYSTEM: Client Accepted! ---");
        close(sockfd);

        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

        return client_fd;
    } else {
        // Client Mode needs timeout on Connect? Standard connect handles it, but we can set it if we want.
        // For now, keep it simple.

        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

        struct hostent *host = gethostbyname(target_ip);
        if (!host) {
            log_to_file("FATAL: Invalid Host/IP");
            return -1;
        }
        memcpy(&s_addr.sin_addr, host->h_addr_list[0], host->h_length);

        log_to_file("--- SYSTEM: Connecting to %s:%d ... ---", target_ip, port);

        while (connect(sockfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
            sleep(RETRY_SEC);
        }
        log_to_file("--- SYSTEM: Connected! ---");
        return sockfd;
    }
}

// Blocking Handshake
int execute_handshake(LinkContext *ctx) {
    char rx_buf[BUFFER_CAP];
    int res, n;
    log_to_file("--- PHASE: Handshake Start ---");

    ctx->buf_start = ctx->buf_end = 0;

    if (ctx->role == MODE_SERVER) {
        send_line(ctx->conn_fd, "ok");

        while(1) {
            n = fill_buffer(ctx);
            if(n < 0) return 0; // EOF/Error

            res = recv_line_buffer(ctx, rx_buf, BUFFER_CAP);
            if(res > 0) {
                log_to_file("RX << '%s'", rx_buf);
                break;
            }
            usleep(1000);
        }

        send_line(ctx->conn_fd, "size %d %d", DYNAMIC_WIDTH, DYNAMIC_HEIGHT);

        while(1) {
            n = fill_buffer(ctx);
            if(n < 0) return 0;

            res = recv_line_buffer(ctx, rx_buf, BUFFER_CAP);
            if(res > 0) {
                log_to_file("RX << '%s'", rx_buf);
                break;
            }
            usleep(1000);
        }

    } else {
        while(1) {
            n = fill_buffer(ctx);
            if(n < 0) return 0;

            res = recv_line_buffer(ctx, rx_buf, BUFFER_CAP);
            if(res > 0) {
                 log_to_file("RX << '%s'", rx_buf);
                 break;
            }
            usleep(1000);
        }

        send_line(ctx->conn_fd, "ook");

        while(1) {
            n = fill_buffer(ctx);
            if(n < 0) return 0;

            res = recv_line_buffer(ctx, rx_buf, BUFFER_CAP);
            if(res > 0) {
                 log_to_file("RX << '%s'", rx_buf);
                 int w=0, h=0;
                 char *ptr = rx_buf;
                 while(*ptr && !isdigit(*ptr)) ptr++;
                 if(*ptr) {
                    w = atoi(ptr);
                    while(*ptr && isdigit(*ptr)) ptr++;
                    while(*ptr && !isdigit(*ptr)) ptr++;
                    if(*ptr) h = atoi(ptr);
                 }
                 if(w > 0 && h > 0) {
                    DYNAMIC_WIDTH = w; DYNAMIC_HEIGHT = h;
                    log_to_file("INFO: Resized to %dx%d", w, h);
                 }
                 break;
            }
            usleep(1000);
        }

        send_line(ctx->conn_fd, "sok");
    }
    log_to_file("--- PHASE: Handshake Complete ---");
    return 1;
}

// ASYNCHRONOUS LOOP
void process_traffic(LinkContext *ctx) {
    DroneState local_drone = {0};
    DroneState remote_obs = {0};
    char line_buf[BUFFER_CAP];
    float x_in, y_in;
    int n_res;

    fd_set readfds;
    struct timeval tv;

    fcntl(ctx->pipe_in_fd, F_SETFL, O_NONBLOCK);

    log_to_file("--- PHASE: Real-time Loop Started ---");

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(ctx->conn_fd, &readfds);
        FD_SET(ctx->pipe_in_fd, &readfds);

        tv.tv_sec = 0;
        tv.tv_usec = 1000;

        int max_fd = (ctx->conn_fd > ctx->pipe_in_fd) ? ctx->conn_fd : ctx->pipe_in_fd;

        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity > 0) {
            // 1. DATA FROM LOCAL GAME -> SEND TO NETWORK
            if (FD_ISSET(ctx->pipe_in_fd, &readfds)) {
                // Drain pipe, get latest
                while (read(ctx->pipe_in_fd, &local_drone, sizeof(DroneState)) > 0);

                // PRIMARY PACKET: Drone Position
                send_line(ctx->conn_fd, "drone");
                send_line(ctx->conn_fd, "%.2f %.2f", local_drone.x, dynamic_invert_axis(local_drone.y));

                // SERVER EXTRA: Send Dummy Obstacles to satisfy Strict Clients
                if(ctx->role == MODE_SERVER) {
                    send_line(ctx->conn_fd, "obst");
                    send_line(ctx->conn_fd, "0"); // 0 obstacles
                    // If client expects targets too, we might need more, but let's try obst first
                }
            }

            if (FD_ISSET(ctx->conn_fd, &readfds)) {
                n_res = fill_buffer(ctx); // Read from socket
                if(n_res < 0) {
                    log_to_file("FATAL: Connection Lost (EOF)");
                    break; // EXIT LOOP ON DISCONNECT
                }

                while (1) {
                    int len = recv_line_buffer(ctx, line_buf, BUFFER_CAP);
                    if (len == 0) break;

                    // SMART PARSER: Handle "drone x y" AND "x y"
                    char *data_ptr = line_buf;
                    if(strncmp(line_buf, "drone", 5) == 0) {
                        data_ptr += 5; // Skip "drone"
                    }

                    // Skip any leading tags/spaces until we find a digit or sign
                    while(*data_ptr && !isdigit(*data_ptr) && *data_ptr != '-' && *data_ptr != '+') {
                        data_ptr++;
                    }

                    if (sscanf(data_ptr, "%f %f", &x_in, &y_in) == 2) {
                        remote_obs.x = x_in;
                        remote_obs.y = dynamic_invert_axis(y_in);
                        write(ctx->pipe_out_fd, &remote_obs, sizeof(DroneState));
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    // Clear log
    FILE *f = fopen("network.log", "w");
    if(f) { fprintf(f, "--- NEW SESSION ---\n"); fclose(f); }

    if (argc < 4) return 1;

    LinkContext ctx = {0};
    ctx.role = atoi(argv[1]);
    ctx.pipe_in_fd = atoi(argv[2]);
    ctx.pipe_out_fd = atoi(argv[3]);

    int port_num = (argc > 4) ? atoi(argv[4]) : COMM_PORT;
    char *ip_addr = (argc > 5) ? argv[5] : "127.0.0.1";

    ctx.conn_fd = establish_link(ctx.role, ip_addr, port_num);
    if (ctx.conn_fd < 0) return 1;

    if (!execute_handshake(&ctx)) {
        log_to_file("FATAL: Handshake Failed");
        close(ctx.conn_fd);
        return 1;
    }

    process_traffic(&ctx);

    close(ctx.conn_fd);
    log_to_file("--- SYSTEM: Network Process Exiting ---");
    return 0;
}
