/* dirtblu.h */
#ifndef SERVER_H
#define SERVER_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <inttypes.h>

#define BACKLOG 10
#define BUF_CHUNK 4096
#define WWW_DIR "www"
#define LOGFILE "access.log"

/* Globals (defined in server.c) */
extern sqlite3 *g_db;
extern pthread_mutex_t db_lock;
extern pthread_mutex_t log_lock;
extern volatile sig_atomic_t keep_running;

/* util.c */
ssize_t send_all(int sock, const void *buf, size_t len);
void write_log(const char *client_ip, const char *req_line, int status_code, size_t bytes_sent);
void url_decode_inplace(char *s);
int parse_form(const char *body, char *name_out, size_t name_sz, int *age_out);
char *find_header_value(const char *headers, const char *name);
ssize_t recv_full_request(int sock, char **out_buf);

/* static.c */
int serve_static_file(int client_sock, const char *path, const char *reqline, const char *client_ip);

/* db.c */
int db_init(const char *dbfile);
char *db_get_all_users_json(void);   /* caller must free */
int db_insert_user(const char *name, int age);

/* router.c */
void *client_thread(void *arg);

/* server.c */
int make_server_socket(const char *bind_ip, int port);

#endif /* SERVER_H */
