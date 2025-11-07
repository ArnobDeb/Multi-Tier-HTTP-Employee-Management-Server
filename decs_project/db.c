#include "db.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct db_conn {
    MYSQL *conn;
};

db_conn_t *db_connect(const char *host, const char *user, const char *pass, const char *dbname) {
    db_conn_t *db = malloc(sizeof(*db));
    db->conn = mysql_init(NULL);
    if (!mysql_real_connect(db->conn, host, user, pass, dbname, 0, NULL, 0)) {
        fprintf(stderr, "MySQL connect error: %s\n", mysql_error(db->conn));
        free(db);
        return NULL;
    }
    return db;
}

void db_disconnect(db_conn_t *db) {
    if (db) {
        mysql_close(db->conn);
        free(db);
    }
}

int db_create_employee(db_conn_t *db, int id, const char *name, const char *dept, double salary) {
    char q[512];
    snprintf(q, sizeof(q),"INSERT INTO employees(id, name, department, salary) VALUES(%d, '%s', '%s', %f)", id, name, dept, salary);
    if (mysql_query(db->conn, q)) {
        fprintf(stderr, "Insert error: %s\n", mysql_error(db->conn));
        return -1;
    }
    return 0;
}

char *db_get_employee(db_conn_t *db, int id) {
    char q[256];
    snprintf(q, sizeof(q), "SELECT name, department, salary FROM employees WHERE id=%d", id);
    if (mysql_query(db->conn, q)) return NULL;
    MYSQL_RES *res = mysql_store_result(db->conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    char *result = NULL;
    if (row) {
        result = malloc(256);
        snprintf(result, 256, "{\"id\":%d,\"name\":\"%s\",\"department\":\"%s\",\"salary\":%s}",
                 id, row[0], row[1], row[2]);
    }
    mysql_free_result(res);
    return result;
}

char *db_get_all_employees(db_conn_t *db) {
    if (mysql_query(db->conn, "SELECT id, name, department, salary FROM employees")) {
        fprintf(stderr, "Query error: %s\n", mysql_error(db->conn));
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(db->conn);
    if (!res) return NULL;

    MYSQL_ROW row;
    size_t buf_size = 1024;
    char *json = malloc(buf_size);
    if (!json) return NULL;

    strcpy(json, "[");
    int first = 1;

    while ((row = mysql_fetch_row(res))) {
        if (!first) strcat(json, ",");
        first = 0;

        // Estimate and grow buffer if needed
        size_t needed = strlen(json) + 256;
        if (needed >= buf_size) {
            buf_size *= 2;
            json = realloc(json, buf_size);
        }

        char record[256];
        snprintf(record, sizeof(record),
                 "{\"id\":%s,\"name\":\"%s\",\"department\":\"%s\",\"salary\":%s}",
                 row[0], row[1], row[2], row[3]);
        strcat(json, record);
    }

    strcat(json, "]");
    mysql_free_result(res);
    return json;
}

int db_update_employee(db_conn_t *db, int id, const char *dept, double salary) {
    char q[512];
    snprintf(q, sizeof(q), "UPDATE employees SET department='%s', salary=%f WHERE id=%d", dept, salary, id);
    return mysql_query(db->conn, q);
}

int db_delete_employee(db_conn_t *db, int id) {
    char q[128];
    snprintf(q, sizeof(q), "DELETE FROM employees WHERE id=%d", id);
    return mysql_query(db->conn, q);
}

int *db_get_all_ids(db_conn_t *db, int *count) {
    if (mysql_query(db->conn, "SELECT id FROM employees")) {
        fprintf(stderr, "Query error: %s\n", mysql_error(db->conn));
        *count = 0;
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(db->conn);
    if (!res) {
        *count = 0;
        return NULL;
    }

    int num_rows = mysql_num_rows(res);
    int *ids = malloc(num_rows * sizeof(int));
    int i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ids[i++] = atoi(row[0]);
    }

    *count = num_rows;
    mysql_free_result(res);
    return ids;
}


