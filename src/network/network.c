/*
 * NETWORK BRIDGE PROCESS (Assignment 3)
 * -------------------------------------
 * This process handles TCP/IP communication for the Multiplayer Mode.
 * It acts as a transparent bridge between the local core (pro_B) and the remote peer.
 *
 * Architecture:
 * - Uses Non-Blocking Sockets and Non-Blocking Pipes.
 * - Uses select() IO Multiplexing to handle traffic without stalling.
 * - Implements a custom line-based protocol over TCP.
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

// --- CONNECTION CONTEXT ---
// Encapsulates all state required for the active link.
// Used to pass data cleanly to helper functions.
typedef struct {
    int conn_fd;        // TCP Socket File Descriptor
    int pipe_in_fd;     // Pipe FROM Server (Local Data)
    int pipe_out_fd;    // Pipe TO Server (Remote Data)
    int role;           // MODE_SERVER or MODE_CLIENT

    // Ring Buffer for handling TCP fragmentation/streams
    // TCP is stream-based, so we might receive half a line or multiple lines at once.
    char net_buffer[BUFFER_CAP];
    int buf_start;
    int buf_end;
} LinkContext;

int map_height = DEFAULT_HEIGHT; // Negotiated during handshake

/*
 * dynamic_invert_axis
 * -------------------
 * Inverts the Y-axis coordinate specifically for network transmission/reception.
 * This ensures that if the coordinate systems differ (e.g., origin top-left vs bottom-left),
 * the visual representation remains correct.
 *
 * Formula: New_Y = Map_Height - Old_Y
 */
float dynamic_invert_axis(float y) {
    if (map_height <= 0) return y;
    return (float)map_height - y;
}

/*
 * log_to_file
 * -----------
 * Writes debug messages to 'network.log'.
 * Accessing stderr/stdout directly would corrupt the Game UI (ncurses),
 * so we redirect all output to a file.
 */
void log_to_file(const char *format, ...) {
    char entry[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(entry, sizeof(entry), format, args);
    va_end(args);

    FILE *f = fopen(LOG_NETWORK, "a");
    if (f) {
        time_t now = time(NULL);
        char *t_str = ctime(&now);
        t_str[strlen(t_str)-1] = 0; // Remove newline
        fprintf(f, "[%s] %s\n", t_str, entry);
        fclose(f);
    }
}

/*
 * send_line
 * ---------
 * Helper to send formatted strings over the TCP socket.
 * Appends a newline '\n' as the protocol delimiter.
 */
ssize_t send_line(int fd, const char *format, ...) {
    char payload[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(payload, sizeof(payload), format, args);
    va_end(args);

    strcat(payload, "\n"); // Protocol Requirement
    ssize_t sent = write(fd, payload, strlen(payload));

    // Log the transmission for debugging
    char debug_buf[1024];
    strncpy(debug_buf, payload, sizeof(debug_buf));
    debug_buf[strcspn(debug_buf, "\n")] = 0;
    log_to_file("TX >> %s", debug_buf);

    return sent;
}

/*
 * recv_line_buffer
 * ----------------
 * circular buffer extraction.
 * Checks if the buffer contains a full line (terminated by '\n').
 * If yes, extracts it and updates buffer pointers.
 *
 * Returns: Length of line if found, 0 otherwise.
 */
int recv_line_buffer(LinkContext *ctx, char *out_line, int max_len) {
    int i = ctx->buf_start;
    int len = 0;

    // Scan from buf_start to buf_end
    while (i != ctx->buf_end) {
        if (ctx->net_buffer[i] == '\n') { // Delimiter found
            int copylen = (len < max_len - 1) ? len : max_len - 1;
            int src_idx = ctx->buf_start;

            // Copy characters to output buffer
            for (int k = 0; k < copylen; k++) {
                out_line[k] = ctx->net_buffer[src_idx];
                src_idx = (src_idx + 1) % BUFFER_CAP;
            }
            out_line[copylen] = '\0';

            ctx->buf_start = (i + 1) % BUFFER_CAP; // Advance Head
            log_to_file("RX << %s", out_line);
            return copylen;
        }

        len++;
        i = (i + 1) % BUFFER_CAP;
    }
    return 0; // No complete line yet
}

/*
 * fill_buffer
 * -----------
 * Reads available bytes from TCP socket into the ring buffer.
 * Handles wrapping at the end of the buffer array.
 */
int fill_buffer(LinkContext *ctx) {
    int space;
    // Calculate contiguous free space
    if (ctx->buf_end >= ctx->buf_start) {
        space = BUFFER_CAP - ctx->buf_end;
        if (ctx->buf_start == 0) space--; // Prevent full overlap
    } else {
        space = ctx->buf_start - ctx->buf_end - 1;
    }

    if (space > 0) {
        int n = read(ctx->conn_fd, &ctx->net_buffer[ctx->buf_end], space);
        if (n > 0) {
            ctx->buf_end = (ctx->buf_end + n) % BUFFER_CAP;
            return 1;
        } else if (n == 0) {
            return -1; // Connection Closed
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_to_file("ERROR: Socket Read Error: %d", errno);
            return -1;
        } else {
            return 0; // No Data (EAGAIN)
        }
    }
    return 0; // Buffer Full
}

/*
 * current_timestamp
 * -----------------
 * Returns current system time in milliseconds.
 * Used for rate-limiting transmissions.
 */
long long current_timestamp() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec*1000LL + te.tv_usec/1000;
}

/*
 * establish_link
 * --------------
 * Creates the TCP socket and connects/binds based on Role.
 * - Server: Binds to Port, Listens, Accepts Client.
 * - Client: Resolves hostname, Connects to Server.
 *
 * Also sets TCP_NODELAY to disable Nagle's Algorithm for lower latency.
 */
int establish_link(int role, int port, const char *target_ip) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_to_file("FATAL: Socket creation failed");
        return -1;
    }

    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    struct sockaddr_in s_addr = {0};
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);

    // Apply Timeouts to the socket
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    log_to_file("--- INIT: Role %d | Port %d ---", role, port);

    if (role == MODE_SERVER) {
        s_addr.sin_addr.s_addr = INADDR_ANY;
        int opt_val = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

        if (bind(sockfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
            log_to_file("FATAL: Bind Failed"); return -1;
        }
        listen(sockfd, 1);

        log_to_file("Waiting for Client...");
        int client_fd = accept(sockfd, NULL, NULL);
        if (client_fd < 0) {
             log_to_file("FATAL: Accept Failed"); return -1;
        }
        log_to_file("Client Connected!");
        close(sockfd); // Close listening socket, keep connected one

        return client_fd;
    } else {
        struct hostent *host = gethostbyname(target_ip);
        if (!host) {
            log_to_file("FATAL: Invalid Host"); return -1;
        }
        memcpy(&s_addr.sin_addr, host->h_addr_list[0], host->h_length);

        log_to_file("Connecting to %s...", target_ip);
        while (connect(sockfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
            sleep(RETRY_SEC);
        }
        log_to_file("Connected to Server!");
        return sockfd;
    }
}

/*
 * execute_handshake
 * -----------------
 * Negotiates session parameters before game starts.
 * Protocol:
 * 1. Hello ("ok" / "ook")
 * 2. Size Exchange ("size W H")
 * 3. Acknowledge ("sok")
 */
int execute_handshake(int fd, int role) {
    char buf[128];
    if (role == MODE_SERVER) {
        send_line(fd, "ok");
        int n = read(fd, buf, sizeof(buf)); // Wait for "ook"
        if (n <= 0) return 0;

        send_line(fd, "size %d %d", DEFAULT_WIDTH, DEFAULT_HEIGHT);
        n = read(fd, buf, sizeof(buf)); // Wait for "sok"
        return (n > 0);
    } else {
        send_line(fd, "ook");
        int n = read(fd, buf, sizeof(buf)); // Wait for "size"
        if (n <= 0) return 0;

        char *ptr = buf;
        while(*ptr && !isdigit(*ptr)) ptr++;
        int w, h;
        sscanf(ptr, "%d %d", &w, &h);
        map_height = h; // Store height for coordinate inversion

        send_line(fd, "sok");
        return 1;
    }
}

/*
 * process_traffic
 * ---------------
 * The core event loop.
 * - Monitors Connection FD and Local Pipe FD using select().
 * - Sending (TX): Rate-limited to 30Hz to avoid network flooding.
 * - Receiving (RX): Batches reads and parses the latest coordinate.
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

    fcntl(ctx->pipe_in_fd, F_SETFL, O_NONBLOCK);

    log_to_file("--- NETWORK BRIDGE ACTIVE ---");

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(ctx->conn_fd, &readfds);
        FD_SET(ctx->pipe_in_fd, &readfds);

        tv.tv_sec = 0;
        tv.tv_usec = 1000; // 1ms Timeout (High responsiveness)

        int max_fd = (ctx->conn_fd > ctx->pipe_in_fd) ? ctx->conn_fd : ctx->pipe_in_fd;
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity > 0) {

            // A) HANDLE OUTGOING (Local -> Remote)
            if (FD_ISSET(ctx->pipe_in_fd, &readfds)) {
                // Drain pipe to get FRESH position
                while (read(ctx->pipe_in_fd, &local_drone, sizeof(DroneState)) > 0);

                long long now = current_timestamp();
                // Limit to 30 FPS (33ms)
                if (now - last_tx_time >= 30) {
                    // Protocol: "drone X Y"
                    send_line(ctx->conn_fd, "drone");
                    send_line(ctx->conn_fd, "%.2f %.2f", local_drone.x, dynamic_invert_axis(local_drone.y));

                    if(ctx->role == MODE_SERVER) {
                         // Send dummy obst to satisfy strict parsers
                        send_line(ctx->conn_fd, "obst");
                        send_line(ctx->conn_fd, "0");
                    }
                    last_tx_time = now;
                }
            }

            // B) HANDLE INCOMING (Remote -> Local)
            if (FD_ISSET(ctx->conn_fd, &readfds)) {
                n_res = fill_buffer(ctx);
                if(n_res < 0) {
                    log_to_file("Disconnected.");
                    break;
                }

                int updated = 0;
                // Parse ALL new lines
                while (1) {
                    int len = recv_line_buffer(ctx, line_buf, BUFFER_CAP);
                    if (len == 0) break;

                    // Parse Logic: Look for coordinates
                    char *data_ptr = line_buf;
                    if(strncmp(line_buf, "drone", 5) == 0) data_ptr += 5;
                    while(*data_ptr && !isdigit(*data_ptr) && *data_ptr != '-' && *data_ptr != '+') data_ptr++;

                    if (sscanf(data_ptr, "%f %f", &x_in, &y_in) == 2) {
                        remote_obs.x = x_in;
                        remote_obs.y = dynamic_invert_axis(y_in);
                        updated = 1;
                    }
                }

                if (updated) {
                    write(ctx->pipe_out_fd, &remote_obs, sizeof(DroneState));
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    // Expected Args: ./network MODE RX_FD TX_FD PORT IP
    if (argc < 6) return 1;

    register_process("Network");
    setup_watchdog_monitor("Network");

    LinkContext ctx = {0};
    ctx.role = atoi(argv[1]);
    ctx.pipe_in_fd = atoi(argv[2]);
    ctx.pipe_out_fd = atoi(argv[3]);
    int port = atoi(argv[4]);
    char *ip = argv[5];

    // 1. Connection
    ctx.conn_fd = establish_link(ctx.role, port, ip);
    if (ctx.conn_fd < 0) return 1;

    // 2. Handshake
    if (!execute_handshake(ctx.conn_fd, ctx.role)) {
        log_to_file("Handshake Failed");
        return 1;
    }

    // 3. Loop
    process_traffic(&ctx);

    close(ctx.conn_fd);
    return 0;
}
