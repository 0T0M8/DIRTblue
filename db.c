// db.c
#include "server.h"

/* Database functions: use g_db and db_lock (declared in server.c) */

/* Initialize DB and create users table if missing */
int db_init(const char *dbfile) {
    if (sqlite3_open(dbfile, &g_db) != SQLITE_OK) {
        fprintf(stderr, "Failed to open DB: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    const char *ddl = "CREATE TABLE IF NOT EXISTS users("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "name TEXT NOT NULL, age INTEGER NOT NULL);";
    char *errmsg = NULL;
    if (sqlite3_exec(g_db, ddl, NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "DDL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

/* Build a JSON array string with all users. Caller frees the returned pointer. */
char *db_get_all_users_json(void) {
    const char *sql = "SELECT id, name, age FROM users ORDER BY id;";
    sqlite3_stmt *stmt = NULL;
    pthread_mutex_lock(&db_lock);
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db_lock);
        return NULL;
    }

    size_t bufcap = 4096;
    size_t buflen = 0;
    char *buf = malloc(bufcap);
    if (!buf) { sqlite3_finalize(stmt); pthread_mutex_unlock(&db_lock); return NULL; }
    buf[0] = '\0';
    buflen = snprintf(buf, bufcap, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        int age = sqlite3_column_int(stmt, 2);
        const char *name_s = name ? (const char*)name : "";
        size_t need = snprintf(NULL, 0, "%s{\"id\":%d,\"name\":\"%s\",\"age\":%d}", first ? "" : ",", id, name_s, age) + 1;
        if (buflen + need + 2 > bufcap) {
            bufcap = (buflen + need + 2) * 2;
            char *tmp = realloc(buf, bufcap);
            if (!tmp) { free(buf); sqlite3_finalize(stmt); pthread_mutex_unlock(&db_lock); return NULL; }
            buf = tmp;
        }
        buflen += snprintf(buf + buflen, bufcap - buflen, "%s{\"id\":%d,\"name\":\"%s\",\"age\":%d}", first ? "" : ",", id, name_s, age);
        first = 0;
    }

    if (buflen + 3 > bufcap) {
        char *tmp = realloc(buf, buflen + 3);
        if (!tmp) { free(buf); sqlite3_finalize(stmt); pthread_mutex_unlock(&db_lock); return NULL; }
        buf = tmp; bufcap = buflen + 3;
    }
    strcat(buf, "]");
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_lock);
    return buf;
}

/* Insert a user and return last insert id, or -1 on error */
int db_insert_user(const char *name, int age) {
    const char *sql = "INSERT INTO users(name, age) VALUES(?, ?);";
    sqlite3_stmt *stmt = NULL;
    pthread_mutex_lock(&db_lock);
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_lock);
        if (stmt) sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, age);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_lock);
        return -1;
    }
    sqlite3_finalize(stmt);
    int last = (int)sqlite3_last_insert_rowid(g_db);
    pthread_mutex_unlock(&db_lock);
    return last;
}
