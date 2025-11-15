// router.c
#define _GNU_SOURCE
#include "server.h"

/* client_thread: receives request, routes to DB handlers or static files */
void *client_thread(void *arg) {
    int client_sock = *(int*)arg; free(arg);
    char *request = NULL;
    ssize_t req_len = recv_full_request(client_sock, &request);
    if (req_len <= 0) { close(client_sock); return NULL; }

    char method[8] = {0}, path[512] = {0}, proto[32] = {0};
    char reqline[1024] = {0};
    char *line_end = strstr(request, "\r\n");
    if (!line_end) line_end = strstr(request, "\n");
    if (line_end) {
        size_t l = (size_t)(line_end - request);
        if (l >= sizeof(reqline)) l = sizeof(reqline)-1;
        strncpy(reqline, request, l);
        reqline[l] = '\0';
    } else {
        strncpy(reqline, request, sizeof(reqline)-1);
    }
    sscanf(reqline, "%7s %511s %31s", method, path, proto);

    /* get client ip */
    struct sockaddr_in addr; socklen_t addrlen = sizeof(addr);
    char client_ip[INET_ADDRSTRLEN] = "unknown";
    if (getpeername(client_sock, (struct sockaddr*)&addr, &addrlen) == 0) {
        inet_ntop(AF_INET, &(addr.sin_addr), client_ip, sizeof(client_ip));
    }

    /* GET /users */
    if (strcasecmp(method, "GET") == 0 && strcmp(path, "/users") == 0) {
        char *json = db_get_all_users_json();
        if (!json) {
            const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client_sock, resp, strlen(resp));
            write_log(client_ip, reqline, 500, 0);
            free(request); close(client_sock); return NULL;
        }
        char header[256];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(json));
        send_all(client_sock, header, header_len);
        send_all(client_sock, json, strlen(json));
        write_log(client_ip, reqline, 200, strlen(json));
        free(json); free(request); close(client_sock); return NULL;
    }

    /* POST /users */
    if (strcasecmp(method, "POST") == 0 && strcmp(path, "/users") == 0) {
        char *headers_end = strstr(request, "\r\n\r\n");
        if (!headers_end) headers_end = strstr(request, "\n\n");
        char *body = headers_end ? (headers_end + ((headers_end[1] == '\n') ? 2 : 4)) : NULL;
        char *ctype = find_header_value(request, "Content-Type");
        char *cl = find_header_value(request, "Content-Length");
        int content_length = 0;
        if (cl) { content_length = atoi(cl); free(cl); }
        char name[256]; int age = -1; int parsed = -1;
        if (body && ctype && strstr(ctype, "application/x-www-form-urlencoded")) {
            if (content_length > 0) body[content_length] = '\0';
            parsed = parse_form(body, name, sizeof(name), &age);
        }
        if (ctype) free(ctype);

        if (parsed == 0) {
            int inserted_id = db_insert_user(name, age);
            if (inserted_id < 0) {
                const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send_all(client_sock, resp, strlen(resp));
                write_log(client_ip, reqline, 500, 0);
            } else {
                char json[512];
                int n = snprintf(json, sizeof(json), "{\"id\":%d,\"name\":\"%s\",\"age\":%d}", inserted_id, name, age);
                char header[256];
                int header_len = snprintf(header, sizeof(header),
                    "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", n);
                send_all(client_sock, header, header_len);
                send_all(client_sock, json, n);
                write_log(client_ip, reqline, 201, n);
            }
        } else {
            const char *bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client_sock, bad, strlen(bad));
            write_log(client_ip, reqline, 400, 0);
        }
        free(request); close(client_sock); return NULL;
    }

    /* Otherwise serve static files */
    if (strcasecmp(method, "GET") == 0) {
        serve_static_file(client_sock, path, reqline, client_ip);
        free(request); close(client_sock); return NULL;
    }

    /* default: method not allowed */
    const char *not_allowed = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send_all(client_sock, not_allowed, strlen(not_allowed));
    write_log(client_ip, reqline, 405, 0);
    free(request); close(client_sock); return NULL;
}
