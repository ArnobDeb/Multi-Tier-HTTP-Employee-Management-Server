#include "civetweb.h"
#include "cache.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ====== Colored Log Macros ======
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_MAGENTA "\033[1;35m"

#define MAX_BODY_SIZE 2048

static cache_t *g_cache;

/* ================== /metrics endpoint ================== */
static int handle_metrics(struct mg_connection *conn, void *ud) {
    mg_printf(conn,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
        "{ \"cache_hits\": %lu, \"cache_misses\": %lu }\n",
        atomic_load(&CACHE_HITS),
        atomic_load(&CACHE_MISSES)
    );
    return 200;
}
static int handle_metrics_reset(struct mg_connection *conn, void *ud) {
    atomic_store(&CACHE_HITS, 0);
    atomic_store(&CACHE_MISSES, 0);
    mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
    return 200;
}

/* ================== Unified employee handler ================== */
static int handle_employee(struct mg_connection *conn, void *ignored) {
    const struct mg_request_info *req_info = mg_get_request_info(conn);
    printf(COLOR_MAGENTA "[Thread %lu]" COLOR_RESET " Handling %s %s\n",
           (unsigned long)pthread_self(), req_info->request_method, req_info->local_uri);

    const char *method = req_info->request_method;
    const char *uri = req_info->local_uri;

    char body[MAX_BODY_SIZE] = {0};
    char emp_id_str[32] = {0};
    int id = 0;

    /* Extract employee ID if present */
    if (strncmp(uri, "/employee/", 10) == 0 && strcmp(uri, "/employee/all") != 0) {
        sscanf(uri + 10, "%d", &id);
        snprintf(emp_id_str, sizeof(emp_id_str), "%d", id);
    }

    if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) {
        mg_read(conn, body, sizeof(body));
    }

    /* Get thread-local DB connection and verify */
    db_conn_t *db = db_get_thread_connection();
    if (db == NULL) {
        printf(COLOR_RED "[DB ERROR]" COLOR_RESET " Thread %lu cannot obtain DB connection\n", (unsigned long)pthread_self());
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length:0\r\n\r\n");
        return 500;
    }

    /* ======================================================
       LIST ALL EMPLOYEES
       ====================================================== */
    if (strcmp(method, "GET") == 0 && strcmp(uri, "/employee/all") == 0) {
        printf(COLOR_CYAN "[LIST]" COLOR_RESET " Request for all employees\n");

        int count = 0;
        int *ids = db_get_all_ids(db, &count);
        if (!ids || count == 0) {
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length:0\r\n\r\n");
            free(ids);
            return 404;
        }

        size_t buf_size = 1024;
        char *json = malloc(buf_size);
        if (!json) { free(ids); mg_printf(conn,"HTTP/1.1 500\r\n\r\n"); return 500; }
        strcpy(json, "[");

        int first = 1;
        for (int i = 0; i < count; i++) {
            int emp_id = ids[i];
            char emp_id_s[16];
            snprintf(emp_id_s, sizeof(emp_id_s), "%d", emp_id);

            /* Check cache */
            char *cached = cache_get(g_cache, emp_id_s);
            if (cached) {
                printf(COLOR_GREEN "[CACHE HIT]" COLOR_RESET " Employee %d (via LIST)\n", emp_id);
                if (!first) strcat(json, ",");
                strcat(json, cached);
                free(cached);
            } else {
                char *emp = db_get_employee(db, emp_id);
                if (emp) {
                    printf(COLOR_YELLOW "[DB FETCH]" COLOR_RESET " Employee %d (LIST)\n", emp_id);
                    cache_put(g_cache, emp_id_s, emp);
                    if (!first) strcat(json, ",");
                    strcat(json, emp);
                    free(emp);
                }
            }

            first = 0;

            /* Grow JSON buffer if required */
            if (strlen(json) > buf_size - 512) {
                buf_size *= 2;
                json = realloc(json, buf_size);
                if (!json) { free(ids); mg_printf(conn,"HTTP/1.1 500\r\n\r\n"); return 500; }
            }
        }

        strcat(json, "]");
        free(ids);

        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json);
        free(json);
        return 200;
    }

    /* ======================================================
       GET one employee
       ====================================================== */
    if (strcmp(method, "GET") == 0) {
        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\n");
            return 400;
        }

        char *cached = cache_get(g_cache, emp_id_str);
        if (cached) {
            printf(COLOR_GREEN "[CACHE HIT]" COLOR_RESET " Employee %s (GET)\n", emp_id_str);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s",
                      cached);
            free(cached);
            return 200;
        }

        char *result = db_get_employee(db, id);

        if (result) {
            printf(COLOR_YELLOW "[DB FETCH]" COLOR_RESET " Employee %s (GET)\n", emp_id_str);
            cache_put(g_cache, emp_id_str, result);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s",
                      result);
            free(result);
            return 200;
        }

        mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\n");
        return 404;
    }

    /* ======================================================
       POST → Create new employee
       ====================================================== */
    if (strcmp(method, "POST") == 0) {
        int id = 0;
        char name[100] = {0}, dept[100] = {0};
        double salary = 0.0;

        sscanf(body, "id=%d&name=%99[^&]&department=%99[^&]&salary=%lf",
               &id, name, dept, &salary);

        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\nInvalid ID");
            return 400;
        }

        if (db_create_employee(db, id, name, dept, salary) == 0) {
            printf(COLOR_BLUE "[DB INSERT]" COLOR_RESET " Employee %d\n", id);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
        } else {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nError");
        }
        return 200;
    }

    /* ======================================================
       PUT → Update employee
       ====================================================== */
    if (strcmp(method, "PUT") == 0) {
        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\n");
            return 400;
        }

        char dept[100] = {0};
        double salary = 0;
        sscanf(body, "department=%99[^&]&salary=%lf", dept, &salary);

        if (db_update_employee(db, id, dept, salary) == 0) {
            printf(COLOR_BLUE "[DB UPDATE]" COLOR_RESET " Employee %d\n", id);
            cache_delete(g_cache, emp_id_str);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
        } else {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nError");
        }
        return 200;
    }

    /* ======================================================
       DELETE employee
       ====================================================== */
    if (strcmp(method, "DELETE") == 0) {
        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\n");
            return 400;
        }

        if (db_delete_employee(db, id) != 0) {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\nError");
            return 500;
        }
        cache_delete(g_cache, emp_id_str);

        printf(COLOR_RED "[DB DELETE]" COLOR_RESET " Employee %d (cache invalidated)\n", id);
        mg_printf(conn, "HTTP/1.1 200 OK\r\n\r\nOK");
        return 200;
    }

    mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
    return 405;
}

/* ======================== main() ======================== */
int main(void) {
    char port_str[8];
    printf("Enter port number to start server on: ");
    scanf("%7s", port_str);

    /* Thread-safe DB initialization */
    db_global_init("localhost", "empuser", "emppass", "company");

    const char *options[] = {
        "listening_ports", port_str,
        "num_threads", "8",       /* CivetWeb worker threads */
        "document_root", "",
        NULL
    };

    g_cache = cache_create(64);

    struct mg_callbacks callbacks = {0};
    struct mg_context *ctx = mg_start(&callbacks, NULL, options);

    mg_set_request_handler(ctx, "/employee", handle_employee, NULL);
    mg_set_request_handler(ctx, "/employee/", handle_employee, NULL);

    mg_set_request_handler(ctx, "/metrics", handle_metrics, NULL);
    mg_set_request_handler(ctx, "/metrics/reset", handle_metrics_reset, NULL);


    printf("Employee server running on http://localhost:%s\n", port_str);
    printf("Press Enter to stop server...\n");
    getchar(); getchar();

    mg_stop(ctx);
    cache_destroy(g_cache);

    /* Cleanup global mysql library state */
    db_global_cleanup();

    return 0;
}
