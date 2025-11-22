#include "civetweb.h"
#include "cache.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mysql.h>

// ====== Colored Log Macros ======
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[1;32m"   // Bright green
#define COLOR_YELLOW  "\033[1;33m"   // Bright yellow
#define COLOR_BLUE    "\033[1;34m"   // Bright blue
#define COLOR_RED     "\033[1;31m"   // Bright red
#define COLOR_CYAN    "\033[1;36m"   // Cyan
#define COLOR_MAGENTA "\033[1;35m"   // Magenta

#define MAX_BODY_SIZE 2048

static cache_t *g_cache;
static db_conn_t *g_db;

/* =================== handle_employee (updated with LIST logic) =================== */
static int handle_employee(struct mg_connection *conn, void *ignored) {
    const struct mg_request_info *req_info = mg_get_request_info(conn);
    printf(COLOR_MAGENTA "[Thread %ld]" COLOR_RESET " Handling %s %s\n", pthread_self(), req_info->request_method, req_info->local_uri);
    //sleep(10);
    //const struct mg_request_info *req_info = mg_get_request_info(conn);
    const char *method = req_info->request_method;
    const char *uri = req_info->local_uri;

    char body[MAX_BODY_SIZE] = {0};
    char emp_id_str[32] = {0};
    int id = 0;

    if (strncmp(uri, "/employee/", 10) == 0 && strcmp(uri, "/employee/all") != 0) {
        sscanf(uri + 10, "%d", &id);
        snprintf(emp_id_str, sizeof(emp_id_str), "%d", id);
    }

    if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0)
        mg_read(conn, body, sizeof(body));

    /* =================== LIST all employees =================== */
    if (strcmp(method, "GET") == 0 && strcmp(uri, "/employee/all") == 0) {
        printf(COLOR_CYAN "[LIST]" COLOR_RESET " Request for all employees\n");

        // Query DB for all employee IDs
        int count = 0;
        int *ids = db_get_all_ids(g_db, &count);
        if (!ids || count == 0) {
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length:0\r\n\r\n");
            free(ids);
            return 404;
        }

        size_t buf_size = 1024;
        char *json = malloc(buf_size);
        strcpy(json, "[");
        int first = 1;

        for (int i = 0; i < count; i++) {
            int emp_id = ids[i];
            char emp_id_s[16];
            snprintf(emp_id_s, sizeof(emp_id_s), "%d", emp_id);

            char *cached = cache_get(g_cache, emp_id_s);
            if (cached) {
                printf(COLOR_GREEN "[CACHE HIT]" COLOR_RESET " Employee %d (via LIST)\n", emp_id);
                if (!first) strcat(json, ",");
                strcat(json, cached);
                free(cached);
            } else {
                char *emp = db_get_employee(g_db, emp_id);
                if (emp) {
                    printf(COLOR_YELLOW "[DB FETCH]" COLOR_RESET " Employee %d (via LIST)\n", emp_id);
                    cache_put(g_cache, emp_id_s, emp);
                    if (!first) strcat(json, ",");
                    strcat(json, emp);
                    free(emp);
                }
            }
            first = 0;

            if (strlen(json) > buf_size - 512) {
                buf_size *= 2;
                json = realloc(json, buf_size);
            }
        }
        strcat(json, "]");
        free(ids);

        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json);
        free(json);
        return 200;
    }

    /* =================== GET one employee =================== */
    if (strcmp(method, "GET") == 0) {
        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Length:0\r\n\r\n");
            return 400;
        }

        char *cached = cache_get(g_cache, emp_id_str);
        if (cached) {
            printf(COLOR_GREEN "[CACHE HIT]" COLOR_RESET " Employee %s (GET)\n", emp_id_str);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", cached);
            free(cached);
            return 200;
        }

        char *result = db_get_employee(g_db, id);
        if (result) {
            printf(COLOR_YELLOW "[DB FETCH]" COLOR_RESET " Employee %s (GET)\n", emp_id_str);
            cache_put(g_cache, emp_id_str, result);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", result);
            free(result);
        } else {
            mg_printf(conn, "HTTP/1.1 404 Not Found\r\nContent-Length:0\r\n\r\n");
        }
        return 200;
    }

    /* =================== POST =================== */
    if (strcmp(method, "POST") == 0) {
        int id = 0;
        char name[100] = {0}, dept[100] = {0};
        double salary = 0.0;
        sscanf(body, "id=%d&name=%99[^&]&department=%99[^&]&salary=%lf", &id, name, dept, &salary);

        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Length:18\r\n\r\nMissing or invalid id");
            return 400;
        }

        if (db_create_employee(g_db, id, name, dept, salary) == 0) {
            printf(COLOR_BLUE "[DB INSERT]" COLOR_RESET " Employee %d\n", id);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
        } else {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length:5\r\n\r\nError");
        }
        return 200;
    }

    /* =================== PUT =================== */
    if (strcmp(method, "PUT") == 0) {
        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Length:0\r\n\r\n");
            return 400;
        }

        char dept[100] = {0};
        double salary = 0.0;
        sscanf(body, "department=%99[^&]&salary=%lf", dept, &salary);

        if (db_update_employee(g_db, id, dept, salary) == 0) {
            printf(COLOR_BLUE "[DB UPDATE]" COLOR_RESET " Employee %d\n", id);
            cache_delete(g_cache, emp_id_str);
            mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
        } else {
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\nContent-Length:5\r\n\r\nError");
        }
        return 200;
    }

    /* =================== DELETE =================== */
    if (strcmp(method, "DELETE") == 0) {
        if (id <= 0) {
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\nContent-Length:0\r\n\r\n");
            return 400;
        }

        db_delete_employee(g_db, id);
        cache_delete(g_cache, emp_id_str);
        printf(COLOR_RED "[DB DELETE]" COLOR_RESET " Employee %d (cache invalidated)\n", id);
        mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
        return 200;
    }
    /* =================== Unsupported =================== */
    mg_printf(conn, "HTTP/1.1 405 Method Not Allowed\r\nContent-Length:0\r\n\r\n");
    return 405;
}

int main(void) {
    char port_str[8];
    printf("Enter port number to start server on: ");
    if (scanf("%7s", port_str) != 1) {
        fprintf(stderr, "Invalid port input.\n");
        return 1;
    }

    const char *options[] = {
        "listening_ports", port_str,
        "document_root", "",
        NULL
    };


    g_cache = cache_create(64);
    g_db = db_connect("localhost", "empuser", "emppass", "company");

    struct mg_callbacks callbacks = {0};
    struct mg_context *ctx = mg_start(&callbacks, NULL, options);

    // Register unified handler for both paths
    mg_set_request_handler(ctx, "/employee", handle_employee, NULL);
    mg_set_request_handler(ctx, "/employee/", handle_employee, NULL);
    printf("Registered handlers for /employee and /employee/\n");


    printf("Employee server running on http://localhost:%s\n", port_str);
    printf("Press Enter to stop server...\n");
    getchar(); getchar();

    mg_stop(ctx);
    db_disconnect(g_db);
    cache_destroy(g_cache);
    return 0;
}