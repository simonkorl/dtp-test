#include <argp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <quiche.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "dtp_config.h"
#include "helper.h"
#include "uthash.h"

/***** Argp configs *****/

const char *argp_program_version = "client-test 0.0.1";
static char doc[] = "libev dtp client for test";
static char args_doc[] = "SERVER_IP SERVER_PORT";
#define ARGS_NUM 2

static struct argp_option options[] = {
    {"log", 'l', "FILE", 0, "log file with debug and error info"},
    {"out", 'o', "FILE", 0, "output file with received file info"},
    {"log-level", 'v', "LEVEL", 0, "log level ERROR 1 -> TRACE 5"},
    {"color", 'c', 0, 0, "log with color"},
    {0}};

struct arguments {
    FILE *log;
    FILE *out;
    char *args[ARGS_NUM];
};

static struct arguments args;

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    switch (key) {
        case 'l':
            arguments->log = fopen(arg, "w+");
            break;
        case 'o':
            arguments->out = fopen(arg, "w+");
            break;
        case 'v':
            LOG_LEVEL = arg ? atoi(arg) : 3;
            break;
        case 'c':
            LOG_COLOR = 1;
            break;
        case ARGP_KEY_ARG:
            if (state->arg_num >= ARGS_NUM) argp_usage(state);
            arguments->args[state->arg_num] = arg;
            break;
        case ARGP_KEY_END:
            if (state->arg_num < ARGS_NUM) argp_usage(state);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

#undef HELPER_LOG
#undef HELPER_OUT
#define HELPER_LOG args.log
#define HELPER_OUT args.out

/***** DTP configs *****/

#define LOCAL_CONN_ID_LEN 16
#define MAX_DATAGRAM_SIZE 1350   // UDP
#define MAX_BLOCK_SIZE 10000000  // QUIC

uint64_t total_bytes = 0;
uint64_t good_bytes = 0;
uint64_t complete_bytes = 0;
uint64_t start_timestamp = 0;
uint64_t end_timestamp = 0;

struct conn_io {
    ev_timer timer;
    ev_timer pace_timer;

    int sock;
    int ai_family;
    quiche_conn *conn;

    uint64_t t_last;
    ssize_t can_send;
    bool done_writing;
};

void set_tos(int ai_family, int sock, int tos) {
    switch (ai_family) {
        case AF_INET:
            if (setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0) {
                log_debug("Warning: Cannot set TOS!");
            }
            break;

        case AF_INET6:
            if (setsockopt(sock, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(int)) <
                0) {
                log_debug("Warning: Cannot set TOS!");
            }
            break;

        default:
            break;
    }
}

static void flush_egress(struct ev_loop *loop, struct conn_io *conn_io) {
    static uint8_t out[MAX_DATAGRAM_SIZE];
    uint64_t rate = quiche_bbr_get_pacing_rate(conn_io->conn);  // bits/s
    // uint64_t rate = 48*1024*1024; //48Mbits/s
    if (conn_io->done_writing) {
        conn_io->can_send = 1350;
        conn_io->t_last = getCurrentUsec();
        conn_io->done_writing = false;
    }

    while (1) {
        uint64_t t_now = getCurrentUsec();
        conn_io->can_send += rate * (t_now - conn_io->t_last) /
                             8000000;  //(bits/8)/s * s = bytes
        // log_debug("%ld us time went, %ld bytes can send",
        //         t_now - conn_io->t_last, conn_io->can_send);
        conn_io->t_last = t_now;
        if (conn_io->can_send < 1350) {
            // log_debug("can_send < 1350");
            conn_io->pace_timer.repeat = 0.001;
            ev_timer_again(loop, &conn_io->pace_timer);
            break;
        }
        // log_debug("send?");
        ssize_t written = quiche_conn_send(conn_io->conn, out, sizeof(out),
                                           NULL, (size_t *)NULL);

        if (written == QUICHE_ERR_DONE) {
            // log_debug("done writing");
            conn_io->pace_timer.repeat = 99999.0;
            ev_timer_again(loop, &conn_io->pace_timer);
            conn_io->done_writing = true;  // app_limited
            break;
        }

        if (written < 0) {
            // log_debug("failed to create packet: %zd", written);
            return;
        }

        int t = 5 << 5;
        set_tos(conn_io->ai_family, conn_io->sock, t);
        ssize_t sent = send(conn_io->sock, out, written, 0);
        if (sent != written) {
            log_error("failed to send");
            return;
        }

        // log_debug("sent %zd bytes", sent);
        conn_io->can_send -= sent;
    }
    double t = quiche_conn_timeout_as_nanos(conn_io->conn) / 1e9f;
    conn_io->timer.repeat = t;
    // log_debug("timeout t = %lf", t);
    ev_timer_again(loop, &conn_io->timer);
}

static void flush_egress_pace(EV_P_ ev_timer *pace_timer, int revents) {
    struct conn_io *conn_io = pace_timer->data;
    // log_debug("begin flush_egress_pace");
    flush_egress(loop, conn_io);
}

static void recv_cb(EV_P_ ev_io *w, int revents) {
    struct conn_io *conn_io = w->data;
    static uint8_t buf[MAX_BLOCK_SIZE];
    uint8_t i = 3;

    while (i--) {
        ssize_t read = recv(conn_io->sock, buf, sizeof(buf), 0);

        if (read < 0) {
            if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
                // log_debug("recv would block");
                break;
            }

            log_error("client failed to read");
            return;
        }

        ssize_t done = quiche_conn_recv(conn_io->conn, buf, read);

        if (done == QUICHE_ERR_DONE) {
            // log_debug("done reading");
            break;
        }

        if (done < 0) {
            // log_debug("failed to process packet");
            return;
        }

        // log_debug("recv %zd bytes", done);
    }

    if (quiche_conn_is_closed(conn_io->conn)) {
        // log_debug("connection closed");
        quiche_stats stats;

        quiche_conn_stats(conn_io->conn, &stats);
        log_info(
            "connection closed, recv=%zu sent=%zu lost=%zu rtt=%fms cwnd=%zu, "
            "total_bytes=%zu, complete_bytes=%zu, good_bytes=%zu, "
            "total_time=%zu\n",
            stats.recv, stats.sent, stats.lost, stats.rtt / 1000.0 / 1000.0,
            stats.cwnd, total_bytes, complete_bytes, good_bytes,
            end_timestamp - start_timestamp

        );
        ev_break(EV_A_ EVBREAK_ONE);
        return;
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        uint64_t s = 0;

        quiche_stream_iter *readable = quiche_conn_readable(conn_io->conn);

        while (quiche_stream_iter_next(readable, &s)) {
            // log_debug("stream %" PRIu64 " is readable", s);

            bool fin = false;
            ssize_t recv_len = quiche_conn_stream_recv(conn_io->conn, s, buf,
                                                       sizeof(buf), &fin);
            total_bytes += recv_len;
            if (recv_len < 0) {
                break;
            }
            if (fin) {
                // output block_size,block_priority,block_deadline
                uint64_t block_size, block_priority, block_deadline;
                int64_t bct = quiche_conn_get_bct(conn_io->conn, s);
                uint64_t goodbytes =
                    quiche_conn_get_good_recv(conn_io->conn, s);
                quiche_conn_get_block_info(conn_io->conn, s, &block_size,
                                           &block_priority, &block_deadline);
                good_bytes += goodbytes;
                complete_bytes += block_size;
                // FILE* clientlog = fopen("client.log", "a+");
                // fprintf(clientlog, "%2ld %14ld %4ld %9ld %5ld %9ld\n", s,
                //         goodbytes, bct, block_size, block_priority,
                //         block_deadline);
                // fclose(clientlog);
                dump_file("%ld,%ld,%ld,%ld,%ld\n", s, bct, block_size,
                          block_priority, block_deadline);
            }

            // if (fin) {
            //     if (quiche_conn_close(conn_io->conn, true, 0, NULL, 0) < 0) {
            //         log_debug("failed to close connection");
            //     }
            // }
        }

        quiche_stream_iter_free(readable);
    }

    flush_egress(loop, conn_io);
}

static void timeout_cb(EV_P_ ev_timer *w, int revents) {
    struct conn_io *conn_io = w->data;
    quiche_conn_on_timeout(conn_io->conn);

    // log_debug("timeout");

    flush_egress(loop, conn_io);

    if (quiche_conn_is_closed(conn_io->conn)) {
        // log_debug("connection closed in timeout ");
        end_timestamp = getCurrentUsec();
        quiche_stats stats;

        quiche_conn_stats(conn_io->conn, &stats);

        // FILE* clientlog = fopen("client.log", "a+");
        // fprintf(clientlog, "connection closed, recv=%zu sent=%zu lost=%zu
        // rtt=%" PRIu64 "ns cwnd=%zu, total_bytes=%zu, complete_bytes=%zu,
        // good_bytes=%zu, total_time=%zu\n",
        //         stats.recv, stats.sent, stats.lost, stats.rtt, stats.cwnd,
        //         total_bytes, complete_bytes, good_bytes, total_time
        //         );
        // fclose(clientlog);
        log_info(
            "connection closed, recv=%zu sent=%zu lost=%zu rtt=%fms cwnd=%zu, "
            "total_bytes=%zu, complete_bytes=%zu, good_bytes=%zu, "
            "total_time=%zu\n",
            stats.recv, stats.sent, stats.lost, stats.rtt / 1000.0 / 1000.0,
            stats.cwnd, total_bytes, complete_bytes, good_bytes,
            end_timestamp - start_timestamp);
        // fprintf(stderr,
        //         "connection closed, recv=%zu sent=%zu lost=%zu rtt=%" PRIu64
        //         "ns\n",
        //         stats.recv, stats.sent, stats.lost, stats.rtt);

        ev_break(EV_A_ EVBREAK_ONE);

        fflush(stdout);
        return;
    }
}

int main(int argc, char *argv[]) {
    args.out = stdout;
    args.log = stderr;
    argp_parse(&argp, argc, argv, 0, 0, &args);
    log_info("SERVER_IP %s SERVER_PORT %s",
             args.args[0], args.args[1]);

    const struct addrinfo hints = {.ai_family = PF_UNSPEC,
                                   .ai_socktype = SOCK_DGRAM,
                                   .ai_protocol = IPPROTO_UDP};

    quiche_enable_debug_logging(debug_log, NULL);

    struct addrinfo *peer;
    if (getaddrinfo(args.args[0], args.args[1], &hints, &peer) != 0) {
        log_error("failed to resolve host");
        return -1;
    }

    int sock = socket(peer->ai_family, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_error("failed to create socket");
        return -1;
    }

    if (fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
        log_error("failed to make socket non-blocking");
        return -1;
    }

    if (connect(sock, peer->ai_addr, peer->ai_addrlen) < 0) {
        log_error("failed to connect socket");
        return -1;
    }

    quiche_config *config = quiche_config_new(0xbabababa);
    if (config == NULL) {
        log_debug("failed to create config");
        return -1;
    }

    quiche_config_set_application_protos(
        config, (uint8_t *)"\x05hq-25\x05hq-24\x05hq-23\x08http/0.9", 15);

    quiche_config_set_max_idle_timeout(config, 5000);
    quiche_config_set_max_packet_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(config, 10000000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 1000000000);
    // quiche_config_set_initial_max_stream_data_uni(config, 1000000000);
    quiche_config_set_initial_max_streams_bidi(config, 10000);
    // quiche_config_set_initial_max_streams_uni(config, 10000);
    // quiche_config_set_disable_active_migration(config, true);
    quiche_config_set_cc_algorithm(config, QUICHE_CC_RENO);
    // quiche_config_set_ntp_server(config, "192.168.0.1");

    if (getenv("SSLKEYLOGFILE")) {
        quiche_config_log_keys(config);
    }

    uint8_t scid[LOCAL_CONN_ID_LEN];
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        log_error("failed to open /dev/urandom");
        return -1;
    }

    ssize_t rand_len = read(rng, &scid, sizeof(scid));
    if (rand_len < 0) {
        log_error("failed to create connection ID");
        return -1;
    }

    quiche_conn *conn = quiche_connect(args.args[0], (const uint8_t *)scid,
                                       sizeof(scid), config);

    start_timestamp = getCurrentUsec();

    if (conn == NULL) {
        log_debug("failed to create connection");
        return -1;
    }

    struct conn_io *conn_io = malloc(sizeof(*conn_io));
    if (conn_io == NULL) {
        log_debug("failed to allocate connection IO");
        return -1;
    }

    // fprintf(stdout, "StreamID goodbytes bct BlockSize Priority Deadline\n");

    dump_file("BlockID,bct,BlockSize,Priority,Deadline\n");
    // FILE* clientlog = fopen("client.log", "w");
    // fprintf(clientlog, "StreamID  bct  BlockSize  Priority  Deadline\n");
    // fclose(clientlog);

    conn_io->sock = sock;
    conn_io->ai_family = peer->ai_family;
    conn_io->conn = conn;
    conn_io->t_last = getCurrentUsec();
    conn_io->can_send = 1350;
    conn_io->done_writing = false;

    ev_io watcher;

    struct ev_loop *loop = ev_default_loop(0);

    ev_io_init(&watcher, recv_cb, conn_io->sock, EV_READ);
    ev_io_start(loop, &watcher);
    watcher.data = conn_io;

    ev_init(&conn_io->timer, timeout_cb);
    conn_io->timer.data = conn_io;

    // ev_timer_init(&conn_io->pace_timer, flush_egress_pace, 99999.0, 99999.0);
    // ev_timer_start(loop, &conn_io->pace_timer);
    ev_init(&conn_io->pace_timer, flush_egress_pace);
    conn_io->pace_timer.data = conn_io;

    flush_egress(loop, conn_io);

    ev_loop(loop, 0);

    freeaddrinfo(peer);

    quiche_conn_free(conn);

    quiche_config_free(config);

    close(sock);

    return 0;
}