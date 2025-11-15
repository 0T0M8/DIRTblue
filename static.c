// static.c
#define _GNU_SOURCE
#include "server.h"

/* Return 0 on success, non-zero on error (404/403) */
int serve_static_file(int client_sock, const char *path, const char *reqline, const char *client_ip) {
    char filepath[1024];
    if (strcmp(path, "/") == 0) snprintf(filepath, sizeof(filepath), "%s/index.html", WWW_DIR);
    else {
        const char *p = path[0] == '/' ? path + 1 : path;
        if (strstr(p, "..")) {
            const char *forb = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client_sock, forb, strlen(forb));
            write_log(client_ip, reqline, 403, 0);
            return -1;
        }
        snprintf(filepath, sizeof(filepath), "%s/%s", WWW_DIR, p);
    }

    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        const char *notf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(client_sock, notf, strlen(notf));
        write_log(client_ip, reqline, 404, 0);
        return -1;
    }

    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    const char *mime = "text/html"; // minimal; could detect by extension
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %jd\r\nConnection: close\r\n\r\n",
        mime, (intmax_t)sz);
    send_all(client_sock, header, header_len);

    ssize_t r;
    char filebuf[4096];
    size_t sent_total = 0;
    while ((r = read(fd, filebuf, sizeof(filebuf))) > 0) {
        if (send_all(client_sock, filebuf, r) == -1) break;
        sent_total += r;
    }
    write_log(client_ip, reqline, 200, sent_total);
    close(fd);
    return 0;
}
