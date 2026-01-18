/*
 * NETWORK MODULE (Assignment 3)
 * -----------------------------
 * This process handles TCP/IP communication between two game instances.
 * It acts as a bridge:
 * 1. Reads local game state from the Server process (via pipe).
 * 2. Sends it over TCP to the remote peer.
 * 3. Receives remote state over TCP.
 * 4. Writes it to the Server process (via pipe).
 */

#include "../include/common.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#define BUFFER_CAP 4096
#define RETRY_SEC 2

// Context for the active connection to avoid passing too many args
typedef struct {
    int conn_fd;        // TCP Socket File Descriptor
    int pipe_in_fd;     // Pipe FROM Server (Local Data)
    int pipe_out_fd;    // Pipe TO Server (Remote Data)
    int role;           // MODE_SERVER or MODE_CLIENT

    // Ring Buffer for handling fragmented TCP packets
    char net_buffer[BUFFER_CAP];
    int buf_start;
    int buf_end;
} LinkContext;

int map_height = DEFAULT_HEIGHT; // Dynamically negotiated during handshake

/* Reverses Y coordinate based on map height (0,0 bottom-left vs top-left) */
float dynamic_invert_axis(float y) {
    if (map_height <= 0) return y;
    return (float)map_height - y;
}

/* Writes log messages to 'network.log' with timestamp. avoids using stderr to keep UI clean. */
void log_to_file(const char *format, ...) {
    char entry[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(entry, sizeof(entry), format, args);
    va_end(args);

    FILE *f = fopen("network.log", "a");
    if (f) {
        time_t now = time(NULL);
        char *t_str = ctime(&now);
        t_str[strlen(t_str)-1] = 0; // trim newline
        fprintf(f, "[%s] %s\n", t_str, entry);
        fclose(f);
    }
}

/* Sends a formatted string over the socket. */
ssize_t send_line(int fd, const char *format, ...) {
    char payload[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(payload, sizeof(payload), format, args);
    va_end(args);

    strcat(payload, "\n"); // Protocol requires newline delimiter
    ssize_t sent = write(fd, payload, strlen(payload));

    // Log what we sent for debugging
    char debug_buf[1024];
    strncpy(debug_buf, payload, sizeof(debug_buf));
    debug_buf[strcspn(debug_buf, "\n")] = 0; // trim for log
    log_to_file("TX >> %s", debug_buf);

    return sent;
}

/*
 * Extracts a single line from the internal ring buffer.
 * Returns length of line found, or 0 if no complete line is available yet.
 */
int recv_line_buffer(LinkContext *ctx, char *out_line, int max_len) {
    int i = ctx->buf_start;
    int len = 0;

    while (i != ctx->buf_end) {
        if (ctx->net_buffer[i] == '\n') { // Found delimiter
            int copylen = (len < max_len - 1) ? len : max_len - 1;
            int src_idx = ctx->buf_start;

            // Copy line to output
            for (int k = 0; k < copylen; k++) {
                out_line[k] = ctx->net_buffer[src_idx];
                src_idx = (src_idx + 1) % BUFFER_CAP;
            }
            out_line[copylen] = '\0';

            ctx->buf_start = (i + 1) % BUFFER_CAP; // Advance buffer start

            log_to_file("RX << %s", out_line); // Log received data
            return copylen;
        }

        len++;
        i = (i + 1) % BUFFER_CAP;
    }
    return 0; // No complete line found
}

/*
 * Reads raw bytes from socket into the ring buffer.
 * Returns 1 if data read, 0 if EAGAIN/Empty, -1 if EOF/Error.
 */
int fill_buffer(LinkContext *ctx) {
    // Determine contiguous space available in ring buffer
    int space;
    if (ctx->buf_end >= ctx->buf_start) {
        space = BUFFER_CAP - ctx->buf_end; // Space until end of array
        if (ctx->buf_start == 0) space--;  // Don't overlap
    } else {
        space = ctx->buf_start - ctx->buf_end - 1;
    }

    if (space > 0) {
        int n = read(ctx->conn_fd, &ctx->net_buffer[ctx->buf_end], space);
        if (n > 0) {
            ctx->buf_end = (ctx->buf_end + n) % BUFFER_CAP;
            return 1;
        } else if (n == 0) {
            return -1; // EOF (Socket Closed)
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_to_file("ERROR: Socket Error: %d", errno);
            return -1;
        } else {
            return 0; // No data currently available
        }
    }
    return 0; // Buffer full
}

/* Returns current time in milliseconds (used for rate limiting). */
long long current_timestamp() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec*1000LL + te.tv_usec/1000;
}

/*
 * Performs the initial Handshake Protocol to establish session.
 * 1. Establish TCP connection.
 * 2. Exchange "ok" messages.
 * 3. Negotiate map dimensions (Client adapts to Server).
 * Returns connected socket FD on success, -1 on failure.
 */
int establish_link(int role, int port, const char *target_ip) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_to_file("FATAL: Socket creation failed");
        return -1;
    }

    // Enable TCP_NODELAY for real-time performance (disable Nagle's algorithm)
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    // Timers - Applied ONLY to connected socket later
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

        // NO TIMEOUT on Listening Socket (waits indefinitely for client)
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

        // Configure connected socket
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

        return client_fd;
    } else {
        // Client Mode
        struct hostent *host = gethostbyname(target_ip);
        if (!host) {
            log_to_file("FATAL: Invalid Host/IP");
            return -1;
        }
        memcpy(&s_addr.sin_addr, host->h_addr_list[0], host->h_length);

        // Apply timeouts for client
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

        log_to_file("--- SYSTEM: Connecting to %s:%d ... ---", target_ip, port);

        while (connect(sockfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
            sleep(RETRY_SEC);
        }
        log_to_file("--- SYSTEM: Connected! ---");
        return sockfd;
    }
}

/*
 * Negotiates map size.
 * Server Sends: "size W H"
 * Client Reads: "size W H", Resizes window, Sends "sok"
 */
int execute_handshake(int fd, int role) {
    char buf[128];
    if (role == MODE_SERVER) {
        send_line(fd, "ok"); // Hello

        // Wait for Client Hello
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) return 0;

        // Send Map Dimensions
        send_line(fd, "size %d %d", DEFAULT_WIDTH, DEFAULT_HEIGHT);

        // Wait for Client Confirmation
        n = read(fd, buf, sizeof(buf));
        return (n > 0);
    } else {
        send_line(fd, "ook");

        // Read "size W H" from Server
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) return 0;

        char *ptr = buf;
        while(*ptr && !isdigit(*ptr)) ptr++;
        sscanf(ptr, "%d %d", &map_height, &map_height); // Note: map_height is global
        // Note: sscanf here gets W first, but we only really care about H for inversion
        // Let's parse strictly to be safe.
        int w, h;
        sscanf(ptr, "%d %d", &w, &h);
        map_height = h;

        send_line(fd, "sok");
        return 1;
    }
}

/*
 * MAIN TRAFFIC LOOP
 * Uses select() to handle bidirectional traffic asynchronously.
 * 1. Limits Transmissions (TX) to 30Hz to prevent lag.
 * 2. Coalesces Received (RX) packets to prevent pipe overflow.
 */
void process_traffic(LinkContext *ctx) {
    DroneState local_drone = {0};
    DroneState remote_obs = {0};
    char line_buf[BUFFER_CAP];
    float x_in, y_in;
    int n_res;

    long long last_tx_time = 0;

    fd_set readfds;
    struct timeval tv;

    // Set pipes to Non-Blocking
    fcntl(ctx->pipe_in_fd, F_SETFL, O_NONBLOCK);

    log_to_file("--- PHASE: Real-time Loop Started ---");

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(ctx->conn_fd, &readfds);
        FD_SET(ctx->pipe_in_fd, &readfds);

        tv.tv_sec = 0;
        tv.tv_usec = 1000; // 1ms select timeout

        int max_fd = (ctx->conn_fd > ctx->pipe_in_fd) ? ctx->conn_fd : ctx->pipe_in_fd;

        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity > 0) {

            // --- TX SECTION (Local Drone -> Network) ---
            if (FD_ISSET(ctx->pipe_in_fd, &readfds)) {
                // Drain local pipe to get the absolutely latest position
                while (read(ctx->pipe_in_fd, &local_drone, sizeof(DroneState)) > 0);

                long long now = current_timestamp();
                // Rate Limiter: Send max 30 times per second (33ms interval)
                if (now - last_tx_time >= 30) {

                    // 1. Send "drone" tag (Required by strict peers)
                    send_line(ctx->conn_fd, "drone");
                    // 2. Send Coordinates
                    send_line(ctx->conn_fd, "%.2f %.2f", local_drone.x, dynamic_invert_axis(local_drone.y));

                    // 3. (Server Only) Send dummy Obstacles to unblock strict clients
                    if(ctx->role == MODE_SERVER) {
                        send_line(ctx->conn_fd, "obst");
                        send_line(ctx->conn_fd, "0");
                    }
                    last_tx_time = now;
                }
            }

            // --- RX SECTION (Network -> Remote Drone Representation) ---
            if (FD_ISSET(ctx->conn_fd, &readfds)) {
                n_res = fill_buffer(ctx); // Read raw bytes
                if(n_res < 0) {
                    log_to_file("FATAL: Connection Lost (EOF)");
                    break; // Exit loop on disconnect
                }

                int updated = 0;
                // Process ALL available lines in the buffer (Packet Coalescing)
                while (1) {
                    int len = recv_line_buffer(ctx, line_buf, BUFFER_CAP);
                    if (len == 0) break;

                    // Smart Parser: Skip "drone" or "obst" prefixes
                    char *data_ptr = line_buf;
                    if(strncmp(line_buf, "drone", 5) == 0) data_ptr += 5;
                    // Find first digit/sign
                    while(*data_ptr && !isdigit(*data_ptr) && *data_ptr != '-' && *data_ptr != '+') data_ptr++;

                    // Parse Coordinates
                    if (sscanf(data_ptr, "%f %f", &x_in, &y_in) == 2) {
                        remote_obs.x = x_in;
                        remote_obs.y = dynamic_invert_axis(y_in);
                        updated = 1; // Mark that we have fresh data
                    }
                }

                // Write to game pipe ONLY ONCE per batch (Smooths movement)
                if (updated) {
                    write(ctx->pipe_out_fd, &remote_obs, sizeof(DroneState));
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    // Arguments: [1]=MODE [2]=PIPE_RX [3]=PIPE_TX [4]=PORT [5]=IP
    if (argc < 6) return 1;

    // Register Process ID for Watchdog
    register_process("Network");
    setup_watchdog_monitor("Network");

    LinkContext ctx = {0};
    ctx.role = atoi(argv[1]);
    ctx.pipe_in_fd = atoi(argv[2]); // Read from Game
    ctx.pipe_out_fd = atoi(argv[3]); // Write to Game
    int port = atoi(argv[4]);
    char *ip = argv[5];

    log_to_file("--- STARTUP: Mode %d, Port %d, Target %s ---", ctx.role, port, ip);

    // 1. Establish TCP Link
    ctx.conn_fd = establish_link(ctx.role, port, ip);
    if (ctx.conn_fd < 0) return 1;

    // 2. Perform Handshake
    if (!execute_handshake(ctx.conn_fd, ctx.role)) {
        log_to_file("Handshake Failed");
        return 1;
    }

    log_to_file("Handshake Success. Entering Loop.");

    // 3. Enter Main Traffic Loop
    process_traffic(&ctx);

    close(ctx.conn_fd);
    return 0;
}
