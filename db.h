#ifndef DB_H
#define DB_H
typedef struct db_conn db_conn_t;

db_conn_t *db_connect(const char *host, const char *user, const char *pass, const char *dbname);
void db_disconnect(db_conn_t *db);
int db_create_employee(db_conn_t *db, int id, const char *name, const char *dept, double salary);
char *db_get_employee(db_conn_t *db, int id);
char *db_get_all_employees(db_conn_t *db);
int db_update_employee(db_conn_t *db, int id, const char *dept, double salary);
int db_delete_employee(db_conn_t *db, int id);
int *db_get_all_ids(db_conn_t *db, int *count);
#endif
