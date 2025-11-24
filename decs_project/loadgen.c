/* loadgen.c — Fully Upgraded Closed-Loop Load Generator
 * Features:
 *  - Multi-threaded
 *  - Closed-loop with zero think time
 *  - Full HTTP parsing (headers + Content-Length)
 *  - Per-request latency logging to CSV
 *  - Robust persistent connection handling
 *  - Workloads: put_all, get_all, get_popular, mix
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE   /* enables random(), strcasestr() on glibc */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>   /* for strcasestr on some systems */
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

typedef enum { WL_PUT_ALL, WL_GET_ALL, WL_GET_POPULAR, WL_MIX } workload_t;

typedef struct {
    const char *host;
    const char *port;
    int thread_id;
    int threads;
    int duration;
    workload_t workload;
    long K;
    long H;
    double p_get, p_put, p_del;
    FILE *csv; // per-thread CSV handle
} thread_arg_t;

typedef struct {
    atomic_ulong success;
    atomic_ulong failures;
    atomic_ulong total_requests;
    atomic_ulong total_latency_ns;
} stats_t;

static stats_t global_stats;
static atomic_ulong global_id = 1;

/* ----------------------- time helper ----------------------- */

static inline long long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ----------------------- connection setup ----------------------- */

static int open_connection(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    int sock = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    return sock;
}

/* ----------------------- send all ----------------------- */

static ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t off = 0;
    const char *b = buf;
    while (off < len) {
        ssize_t n = send(fd, b + off, len - off, 0);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += n;
    }
    return off;
}

/* ----------------------- HTTP response parser ----------------------- */

static int read_http_response(int sock) {
    char header[8192];
    int header_len = 0;

    // 1) Read until "\r\n\r\n"
    while (header_len < sizeof(header)-1) {
        ssize_t r = recv(sock, header + header_len, 1, 0);
        if (r <= 0) return -1;
        header_len += r;
        if (header_len >= 4 &&
            memcmp(header + header_len - 4, "\r\n\r\n", 4) == 0) break;
    }
    header[header_len] = '\0';

    // 2) Extract HTTP status
    int status = 0;
    char *p = strstr(header, "HTTP/");
    if (!p) return -1;
    char *sp = strchr(p, ' ');
    if (!sp) return -1;
    status = atoi(sp + 1);

    // 3) Parse Content-Length
    int content_len = 0;
    char *cl = strcasestr(header, "Content-Length:");
    if (cl) {
        content_len = atoi(cl + strlen("Content-Length:"));
    }

    // 4) Read body completely
    int remain = content_len;
    char bodybuf[4096];
    while (remain > 0) {
        int to_read = (remain > sizeof(bodybuf)) ? sizeof(bodybuf) : remain;
        ssize_t r = recv(sock, bodybuf, to_read, 0);
        if (r <= 0) return -1;
        remain -= r;
    }

    return (status >= 200 && status < 300) ? 0 : -1;
}
/* ----------------------- random helper ----------------------- */

static long rnd_range(long lo, long hi) {
    if (hi <= lo) return lo;
    return (random() % (hi - lo + 1)) + lo;
}

/* ----------------------- HTTP request wrapper ----------------------- */

static int do_http(int sock, const char *req, long long *lat_ns) {
    long long t0 = now_ns();

    if (send_all(sock, req, strlen(req)) < 0)
        return -1;

    int ok = read_http_response(sock);

    long long t1 = now_ns();
    *lat_ns = t1 - t0;

    return ok;
}

/* ----------------------- Worker ----------------------- */

static void *worker(void *arg) {
    thread_arg_t *t = arg;

    int sock = open_connection(t->host, t->port);
    if (sock < 0) {
        fprintf(stderr, "Thread %d: Unable to connect\n", t->thread_id);
        return NULL;
    }

    long end = time(NULL) + t->duration;

    /* per-thread seed variable */
    unsigned int seed = (unsigned int)(time(NULL) ^ (t->thread_id * 7919));

    while (time(NULL) < end) {

        char req[2048];
        long long lat_ns = 0;
        int rc = -1;

        switch (t->workload) {

        case WL_PUT_ALL: {
            long id = atomic_fetch_add(&global_id, 1);
            char body[256];
            snprintf(body, sizeof(body),
                     "id=%ld&name=User%ld&department=GEN&salary=1000", id, id);

            snprintf(req, sizeof(req),
                "POST /employee HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: application/x-www-form-urlencoded\r\n"
                "Content-Length: %zu\r\n\r\n"
                "%s",
                t->host, strlen(body), body);

            rc = do_http(sock, req, &lat_ns);
            break;
        }

        case WL_GET_ALL: {
            static _Thread_local long cnt = 0;
            cnt++;
            long id = (cnt % t->K) + 1;

            snprintf(req, sizeof(req),
                "GET /employee/%ld HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: keep-alive\r\n\r\n", id, t->host);

            rc = do_http(sock, req, &lat_ns);
            break;
        }

        case WL_GET_POPULAR: {
            double r = (double)rand_r(&seed) / RAND_MAX;
            long id = (r < 0.9) ? rnd_range(1, t->H) : rnd_range(1, t->K);

            snprintf(req, sizeof(req),
                "GET /employee/%ld HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: keep-alive\r\n\r\n", id, t->host);

            rc = do_http(sock, req, &lat_ns);
            break;
        }

        case WL_MIX: {
            double r = (double)rand_r(&seed) / RAND_MAX;

            if (r < t->p_get) {
                long id = rnd_range(1, t->K);
                snprintf(req, sizeof(req),
                    "GET /employee/%ld HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Connection: keep-alive\r\n\r\n", id, t->host);

                rc = do_http(sock, req, &lat_ns);

            } else if (r < t->p_get + t->p_put) {

                long id = atomic_fetch_add(&global_id, 1);
                char body[256];
                snprintf(body, sizeof(body),
                         "id=%ld&name=User%ld&department=GEN&salary=1000", id, id);

                snprintf(req, sizeof(req),
                    "POST /employee HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Connection: keep-alive\r\n"
                    "Content-Type: application/x-www-form-urlencoded\r\n"
                    "Content-Length: %zu\r\n\r\n"
                    "%s",
                    t->host, strlen(body), body);

                rc = do_http(sock, req, &lat_ns);

            } else {
                long id = rnd_range(1, t->K);
                snprintf(req, sizeof(req),
                    "DELETE /employee/%ld HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Connection: keep-alive\r\n\r\n", id, t->host);

                rc = do_http(sock, req, &lat_ns);
            }
            break;
        }
        }

        atomic_fetch_add(&global_stats.total_requests, 1);

        if (rc == 0) {
            atomic_fetch_add(&global_stats.success, 1);
            atomic_fetch_add(&global_stats.total_latency_ns, lat_ns);
            fprintf(t->csv, "%lld\n", lat_ns);
        } else {
            atomic_fetch_add(&global_stats.failures, 1);
            close(sock);
            sock = open_connection(t->host, t->port);
            if (sock < 0) break;
        }
    }

    close(sock);
    return NULL;
}

/* ----------------------- Main ----------------------- */

static void usage(const char *p) {
    printf("Usage: %s host port threads duration workload <params>\n", p);
}

int main(int argc, char **argv) {

    if (argc < 6) { usage(argv[0]); return 1; }

    const char *host = argv[1];
    const char *port = argv[2];
    int threads = atoi(argv[3]);
    int duration = atoi(argv[4]);
    const char *w = argv[5];

    workload_t wl;
    long K = 1000, H = 10;
    double p_get = 0.6, p_put = 0.3, p_del = 0.1;

    if (!strcmp(w,"put_all")) wl = WL_PUT_ALL;
    else if (!strcmp(w,"get_all")) {
        wl = WL_GET_ALL;
        K = atol(argv[6]);
    }
    else if (!strcmp(w,"get_popular")) {
        wl = WL_GET_POPULAR;
        K = atol(argv[6]);
        H = atol(argv[7]);
    }
    else if (!strcmp(w,"mix")) {
        wl = WL_MIX;
        // Defensive parsing: check argc before accessing argv[] entries
        if (argc >= 7) p_get = atof(argv[6]);    // default 0.6 otherwise
        if (argc >= 8) p_put = atof(argv[7]);    // default 0.3 otherwise
        if (argc >= 9) p_del = atof(argv[8]);    // default 0.1 otherwise
        if (argc >= 10) K = atol(argv[9]);       // default 1000 otherwise
        if (argc >= 11) H = atol(argv[10]);      // default 10 otherwise

        // Validate probabilities (optional)
        if (p_get < 0.0 || p_get > 1.0) p_get = 0.6;
        if (p_put < 0.0 || p_put > 1.0) p_put = 0.3;
        if (p_del < 0.0 || p_del > 1.0) p_del = 0.1;
        if (K <= 0) K = 1000;
        if (H <= 0) H = 10;
    } else {
        usage(argv[0]);
        return 1;
    }

    atomic_store(&global_stats.success, 0);
    atomic_store(&global_stats.failures, 0);
    atomic_store(&global_stats.total_requests, 0);
    atomic_store(&global_stats.total_latency_ns, 0);

    pthread_t tids[threads];
    thread_arg_t args[threads];

    char csvname[64];
    snprintf(csvname, sizeof(csvname), "latencies_%s.csv", w);
    FILE *csv = fopen(csvname, "w");

    fprintf(csv, "latency_ns\n");

    for (int i=0; i<threads; i++) {
        args[i].host = host;
        args[i].port = port;
        args[i].thread_id = i;
        args[i].threads = threads;
        args[i].duration = duration;
        args[i].workload = wl;
        args[i].K = K;
        args[i].H = H;
        args[i].p_get = p_get;
        args[i].p_put = p_put;
        args[i].p_del = p_del;
        args[i].csv = csv;
        pthread_create(&tids[i], NULL, worker, &args[i]);
    }

    for (int i=0; i<threads; i++)
        pthread_join(tids[i], NULL);

    fclose(csv);

    unsigned long succ = atomic_load(&global_stats.success);
    unsigned long tot = atomic_load(&global_stats.total_requests);
    unsigned long fail = atomic_load(&global_stats.failures);
    unsigned long lat = atomic_load(&global_stats.total_latency_ns);

    printf("SUCCESS=%lu FAIL=%lu TOTAL=%lu\n", succ, fail, tot);
    printf("THROUGHPUT=%.2f req/s\n", (double)succ / duration);
    printf("AVG_LATENCY=%.3f ms\n", (succ ? ((double)lat/succ)/1e6 : 0.0));

    return 0;
}
