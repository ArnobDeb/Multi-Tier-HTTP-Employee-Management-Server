#include "db.h"
#include <mysql/mysql.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   Global DB parameters (stored once)
   ============================================================ */
static char g_host[64], g_user[64], g_pass[64], g_dbname[64];

/* Thread-local key for db_conn_t */
static pthread_key_t db_key;
static pthread_once_t db_key_once = PTHREAD_ONCE_INIT;

/* ============================================================
   Thread-local destructor
   ============================================================ */
static void db_destroy(void *ptr) {
    db_conn_t *db = (db_conn_t *)ptr;
    if (db) {
        if (db->conn) {
            mysql_close(db->conn);
            db->conn = NULL;
        }
        /* per-thread cleanup */
        mysql_thread_end();
        free(db);
    }
}

/* ============================================================
   Create TLS key once
   ============================================================ */
static void db_make_key() {
    pthread_key_create(&db_key, db_destroy);
}

/* ============================================================
   Global init
   ============================================================ */
void db_global_init(const char *host, const char *user, const char *pass, const char *dbname) {
    /* store parameters */
    strncpy(g_host, host ? host : "", sizeof(g_host)-1);
    strncpy(g_user, user ? user : "", sizeof(g_user)-1);
    strncpy(g_pass, pass ? pass : "", sizeof(g_pass)-1);
    strncpy(g_dbname, dbname ? dbname : "", sizeof(g_dbname)-1);
    g_host[sizeof(g_host)-1] = '\0';
    g_user[sizeof(g_user)-1] = '\0';
    g_pass[sizeof(g_pass)-1] = '\0';
    g_dbname[sizeof(g_dbname)-1] = '\0';

    /* initialize mysql client library (thread-safe) */
    if (mysql_library_init(0, NULL, NULL)) {
        fprintf(stderr, "mysql_library_init() failed\n");
    }

    pthread_once(&db_key_once, db_make_key);
}

/* ============================================================
   Thread-local connection getter
   ============================================================ */
db_conn_t *db_get_thread_connection() {
    pthread_once(&db_key_once, db_make_key);

    db_conn_t *db = pthread_getspecific(db_key);
    if (db != NULL)
        return db;

    /* per-thread library init */
    if (mysql_thread_init() != 0) {
        fprintf(stderr, "mysql_thread_init() failed for thread\n");
        return NULL;
    }

    /* First time use → create connection */
    db = malloc(sizeof(db_conn_t));
    if (!db) return NULL;
    memset(db, 0, sizeof(*db));

    db->conn = mysql_init(NULL);
    if (!db->conn) {
        fprintf(stderr, "mysql_init() failed\n");
        mysql_thread_end();
        free(db);
        return NULL;
    }

    /* connect: use default port (0) to pick UNIX socket when appropriate */
    if (!mysql_real_connect(db->conn, g_host, g_user, g_pass, g_dbname, 0, NULL, 0)) {
        fprintf(stderr, "MySQL connection error (thread-local): %s\n", mysql_error(db->conn));
        mysql_close(db->conn);
        mysql_thread_end();
        free(db);
        return NULL;
    }

    pthread_setspecific(db_key, db);
    return db;
}

/* ============================================================
   Helper: reconnect if needed
   ============================================================ */
static int ensure_connection(db_conn_t **pdb) {
    if (*pdb == NULL) {
        *pdb = db_get_thread_connection();
        if (*pdb == NULL) return -1;
    }
    if (mysql_ping((*pdb)->conn) != 0) {
        /* try to reconnect */
        mysql_close((*pdb)->conn);
        (*pdb)->conn = mysql_init(NULL);
        if (!(*pdb)->conn) {
            fprintf(stderr, "mysql_init() failed on reconnect\n");
            return -1;
        }
        if (!mysql_real_connect((*pdb)->conn, g_host, g_user, g_pass, g_dbname, 0, NULL, 0)) {
            fprintf(stderr, "Reconnect failed: %s\n", mysql_error((*pdb)->conn));
            mysql_close((*pdb)->conn);
            (*pdb)->conn = NULL;
            return -1;
        }
    }
    return 0;
}

/* ============================================================
   CRUD operations (use thread-local db_conn)
   Each function now checks ensure_connection() and behaves
   gracefully if connection cannot be obtained.
   ============================================================ */

int db_create_employee(db_conn_t *db, int id, const char *name, const char *dept, double salary) {
    if (ensure_connection(&db) != 0) {
        return -1;
    }

    char q[512];
    snprintf(q, sizeof(q),
        "INSERT INTO employees(id, name, department, salary) VALUES(%d, '%s', '%s', %f)",
        id, name ? name : "", dept ? dept : "", salary);

    if (mysql_query(db->conn, q)) {
        fprintf(stderr, "Insert error: %s\n", mysql_error(db->conn));
        return -1;
    }
    return 0;
}

char *db_get_employee(db_conn_t *db, int id) {
    if (ensure_connection(&db) != 0) {
        return NULL;
    }

    char q[256];
    snprintf(q, sizeof(q), "SELECT name, department, salary FROM employees WHERE id=%d", id);

    if (mysql_query(db->conn, q)) {
        fprintf(stderr, "Query error: %s\n", mysql_error(db->conn));
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(db->conn);
    if (!res) return NULL;

    MYSQL_ROW row = mysql_fetch_row(res);
    char *result = NULL;

    if (row) {
        result = malloc(256);
        snprintf(result, 256,
                 "{\"id\":%d,\"name\":\"%s\",\"department\":\"%s\",\"salary\":%s}",
                 id, row[0] ? row[0] : "", row[1] ? row[1] : "", row[2] ? row[2] : "0");
    }
    mysql_free_result(res);
    return result;
}

char *db_get_all_employees(db_conn_t *db) {
    if (ensure_connection(&db) != 0) {
        return NULL;
    }

    if (mysql_query(db->conn, "SELECT id, name, department, salary FROM employees")) {
        fprintf(stderr, "Query error: %s\n", mysql_error(db->conn));
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(db->conn);
    if (!res) return NULL;

    MYSQL_ROW row;
    size_t buf_size = 1024;
    char *json = malloc(buf_size);
    if (!json) { mysql_free_result(res); return NULL; }
    json[0] = '\0';
    strcat(json, "[");

    int first = 1;
    while ((row = mysql_fetch_row(res))) {
        if (!first) strcat(json, ",");
        first = 0;

        size_t needed = strlen(json) + 256;
        if (needed >= buf_size) {
            buf_size *= 2;
            json = realloc(json, buf_size);
            if (!json) { mysql_free_result(res); return NULL; }
        }

        char rec[256];
        snprintf(rec, sizeof(rec),
                 "{\"id\":%s,\"name\":\"%s\",\"department\":\"%s\",\"salary\":%s}",
                 row[0] ? row[0] : "0", row[1] ? row[1] : "", row[2] ? row[2] : "", row[3] ? row[3] : "0");
        strcat(json, rec);
    }

    strcat(json, "]");
    mysql_free_result(res);
    return json;
}

int db_update_employee(db_conn_t *db, int id, const char *dept, double salary) {
    if (ensure_connection(&db) != 0) {
        return -1;
    }

    char q[512];
    snprintf(q, sizeof(q),
             "UPDATE employees SET department='%s', salary=%f WHERE id=%d",
             dept ? dept : "", salary, id);

    if (mysql_query(db->conn, q)) {
        fprintf(stderr, "Update error: %s\n", mysql_error(db->conn));
        return -1;
    }
    return 0;
}

int db_delete_employee(db_conn_t *db, int id) {
    if (ensure_connection(&db) != 0) {
        return -1;
    }

    char q[128];
    snprintf(q, sizeof(q), "DELETE FROM employees WHERE id=%d", id);

    if (mysql_query(db->conn, q)) {
        fprintf(stderr, "Delete error: %s\n", mysql_error(db->conn));
        return -1;
    }
    return 0;
}

int *db_get_all_ids(db_conn_t *db, int *count) {
    *count = 0;
    if (ensure_connection(&db) != 0) {
        return NULL;
    }

    if (mysql_query(db->conn, "SELECT id FROM employees")) {
        fprintf(stderr, "Query error: %s\n", mysql_error(db->conn));
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(db->conn);
    if (!res) { *count = 0; return NULL; }

    int rows = mysql_num_rows(res);
    int *ids = malloc(rows * sizeof(int));
    if (!ids) { mysql_free_result(res); *count = 0; return NULL; }

    MYSQL_ROW row;
    int i = 0;
    while ((row = mysql_fetch_row(res))) {
        ids[i++] = atoi(row[0] ? row[0] : "0");
    }

    *count = rows;
    mysql_free_result(res);
    return ids;
}

/* ============================================================
   Cleanup all thread-local DB connections
   ============================================================ */
void db_global_cleanup() {
    /* mysql_library_end cleans up global library state */
    mysql_library_end();
}
