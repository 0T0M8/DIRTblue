// dirtblue.c
#include "server.h"

/* Globals definition */
sqlite3 *g_db = NULL;
pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t keep_running = 1;

static int server_fd = -1;

/* Signal handler and server socket creator */
void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
    if (server_fd != -1) close(server_fd);
}

int make_server_socket(const char *bind_ip, int port) {
    int s;
    int opt = 1;
    struct sockaddr_in addr;
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) { perror("socket"); return -1; }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) { perror("setsockopt"); close(s); return -1; }
    addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
    if (inet_aton(bind_ip, &addr.sin_addr) == 0) { fprintf(stderr, "Invalid bind IP: %s\n", bind_ip); close(s); return -1; }
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == -1) { perror("bind"); close(s); return -1; }
    if (listen(s, BACKLOG) == -1) { perror("listen"); close(s); return -1; }
    return s;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "Usage: %s <port> <bind_ip>\n", argv[0]); return 1; }
    int port = atoi(argv[1]); const char *bind_ip = argv[2];

    mkdir(WWW_DIR, 0755);
    if (db_init("mydb.db") != 0) { fprintf(stderr, "Database init failed\n"); return 1; }

    signal(SIGINT, handle_sigint);
    server_fd = make_server_socket(bind_ip, port);
    if (server_fd == -1) return 1;
    printf("Server (modular) listening on %s:%d  (www dir: ./%s)\n", bind_ip, port, WWW_DIR);
    fflush(stdout);

    while (keep_running) {
        struct sockaddr_in client_addr; socklen_t addrlen = sizeof(client_addr);
        int *client_sock = malloc(sizeof(int));
        if (!client_sock) continue;
        *client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (*client_sock == -1) {
            free(client_sock);
            if (errno == EINTR) break;
            perror("accept"); continue;
        }
        pthread_t tid; pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, client_thread, client_sock) != 0) {
            perror("pthread_create"); close(*client_sock); free(client_sock);
        }
        pthread_attr_destroy(&attr);
    }

    if (server_fd != -1) close(server_fd);
    if (g_db) sqlite3_close(g_db);
    printf("Server shutting down.\n");
    return 0;
}
