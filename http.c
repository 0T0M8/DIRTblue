// http.c
#include "server.h"

/* Basic safe send_all */
ssize_t send_all(int sock, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = buf;
    while (total < len) {
        ssize_t sent = send(sock, p + total, len - total, 0);
        if (sent <= 0) {
            if (sent == -1 && errno == EINTR) continue;
            return -1;
        }
        total += sent;
    }
    return (ssize_t)total;
}

/* Thread-safe logger */
void write_log(const char *client_ip, const char *req_line, int status_code, size_t bytes_sent) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen(LOGFILE, "a");
    if (f) {
        fprintf(f, "%s - \"%s\" %d %zu\n", client_ip, req_line ? req_line : "-", status_code, bytes_sent);
        fclose(f);
    }
    pthread_mutex_unlock(&log_lock);
}

/* URL decode */
void url_decode_inplace(char *s) {
    char *dst = s, *src = s;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char) strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* parse form "name=Bob&age=30" */
int parse_form(const char *body, char *name_out, size_t name_sz, int *age_out) {
    if (!body) return -1;
    char *buf = strdup(body);
    if (!buf) return -1;
    char *token = strtok(buf, "&");
    name_out[0] = '\0'; *age_out = -1;
    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char *k = token; char *v = eq + 1;
            if (strcmp(k, "name") == 0) {
                strncpy(name_out, v, name_sz-1);
                name_out[name_sz-1] = '\0';
                url_decode_inplace(name_out);
            } else if (strcmp(k, "age") == 0) {
                char tmp[32]; strncpy(tmp, v, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
                url_decode_inplace(tmp); *age_out = atoi(tmp);
            }
        }
        token = strtok(NULL, "&");
    }
    free(buf);
    if (name_out[0] == '\0' || *age_out < 0) return -1;
    return 0;
}

/* find header value (case-insensitive), returns malloc'd string or NULL */
char *find_header_value(const char *headers, const char *name) {
    if (!headers || !name) return NULL;
    const char *p = headers; size_t namelen = strlen(name);
    while (*p) {
        const char *line_end = strstr(p, "\r\n");
        size_t linelen = line_end ? (size_t)(line_end - p) : strlen(p);
        if (linelen >= namelen) {
            if (strncasecmp(p, name, namelen) == 0 && p[namelen] == ':') {
                const char *val = p + namelen + 1;
                while (*val == ' ') val++;
                size_t vlen = line_end ? (size_t)(line_end - val) : strlen(val);
                char *out = malloc(vlen + 1);
                if (!out) return NULL;
                strncpy(out, val, vlen); out[vlen] = '\0';
                return out;
            }
        }
        if (!line_end) break;
        p = line_end + 2;
    }
    return NULL;
}

/* recv until headers + body (Content-Length) are fully received */
ssize_t recv_full_request(int sock, char **out_buf) {
    size_t cap = BUF_CHUNK, len = 0;
    char *buf = malloc(cap);
    if (!buf) return -1;
    buf[0] = '\0';
    ssize_t n;
    char tmp[BUF_CHUNK];
    int headers_found = 0; int content_length = 0; size_t needed = 0;

    while (1) {
        n = recv(sock, tmp, sizeof(tmp), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf); return -1;
        } else if (n == 0) {
            break;
        }
        if (len + (size_t)n + 1 > cap) {
            cap = (len + (size_t)n + 1) * 2;
            char *t = realloc(buf, cap); if (!t) { free(buf); return -1; } buf = t;
        }
        memcpy(buf + len, tmp, n); len += (size_t)n; buf[len] = '\0';

        if (!headers_found) {
            char *headers_end = strstr(buf, "\r\n\r\n");
            if (!headers_end) headers_end = strstr(buf, "\n\n");
            if (headers_end) {
                headers_found = 1;
                char *cl = find_header_value(buf, "Content-Length");
                if (cl) { content_length = atoi(cl); free(cl); } else content_length = 0;
                char *body_start = strstr(buf, "\r\n\r\n");
                if (!body_start) body_start = strstr(buf, "\n\n");
                if (body_start) {
                    size_t header_len = (size_t)((body_start - buf) + ((body_start[1] == '\n') ? 2 : 4));
                    needed = header_len + (size_t)content_length;
                } else {
                    needed = (size_t)content_length;
                }
            }
        }
        if (headers_found && len >= needed) break;
    }

    *out_buf = buf;
    return (ssize_t)len;
}

/* client handler: routes /users and serves static files */
void *client_thread(void *arg) {
    int client_sock = *(int*)arg; free(arg);
    char *request = NULL;
    ssize_t req_len = recv_full_request(client_sock, &request);
    if (req_len <= 0) { close(client_sock); return NULL; }

    char method[8]={0}, path[512]={0}, proto[32]={0};
    char reqline[1024]={0};
    char *line_end = strstr(request, "\r\n");
    if (!line_end) line_end = strstr(request, "\n");
    if (line_end) {
        size_t l = (size_t)(line_end - request);
        if (l >= sizeof(reqline)) l = sizeof(reqline)-1;
        strncpy(reqline, request, l); reqline[l] = '\0';
    } else {
        strncpy(reqline, request, sizeof(reqline)-1);
    }
    sscanf(reqline, "%7s %511s %31s", method, path, proto);

    struct sockaddr_in addr; socklen_t addrlen = sizeof(addr);
    char client_ip[INET_ADDRSTRLEN] = "unknown";
    if (getpeername(client_sock, (struct sockaddr*)&addr, &addrlen) == 0) inet_ntop(AF_INET, &(addr.sin_addr), client_ip, sizeof(client_ip));

    /* GET /users */
    if (strcasecmp(method, "GET") == 0 && strcmp(path, "/users") == 0) {
        char *json = db_get_all_users_json();
        if (!json) {
            const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client_sock, resp, strlen(resp)); write_log(client_ip, reqline, 500, 0);
            free(request); close(client_sock); return NULL;
        }
        char header[256];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(json));
        send_all(client_sock, header, header_len); send_all(client_sock, json, strlen(json));
        write_log(client_ip, reqline, 200, strlen(json)); free(json); free(request); close(client_sock); return NULL;
    }

    /* POST /users */
    if (strcasecmp(method, "POST") == 0 && strcmp(path, "/users") == 0) {
        char *headers_end = strstr(request, "\r\n\r\n"); if (!headers_end) headers_end = strstr(request, "\n\n");
        char *body = headers_end ? (headers_end + ((headers_end[1] == '\n') ? 2 : 4)) : NULL;
        char *ctype = find_header_value(request, "Content-Type");
        char *cl = find_header_value(request, "Content-Length");
        int content_length = 0; if (cl) { content_length = atoi(cl); free(cl); }
        char name[256]; int age=-1; int parsed=-1;
        if (body && ctype && strstr(ctype, "application/x-www-form-urlencoded")) {
            if (content_length > 0) body[content_length] = '\0';
            parsed = parse_form(body, name, sizeof(name), &age);
        }
        if (ctype) free(ctype);

        if (parsed == 0) {
            int inserted_id = db_insert_user(name, age);
            if (inserted_id < 0) {
                const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send_all(client_sock, resp, strlen(resp)); write_log(client_ip, reqline, 500, 0);
            } else {
                char json[512]; int n = snprintf(json, sizeof(json), "{\"id\":%d,\"name\":\"%s\",\"age\":%d}", inserted_id, name, age);
                char header[256]; int header_len = snprintf(header, sizeof(header),
                    "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", n);
                send_all(client_sock, header, header_len); send_all(client_sock, json, n); write_log(client_ip, reqline, 201, n);
            }
        } else {
            const char *bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client_sock, bad, strlen(bad)); write_log(client_ip, reqline, 400, 0);
        }
        free(request); close(client_sock); return NULL;
    }

    /* Serve static GET files (same logic as before) */
    if (strcasecmp(method, "GET") == 0) {
        char filepath[1024];
        if (strcmp(path, "/") == 0) snprintf(filepath, sizeof(filepath), "%s/index.html", WWW_DIR);
        else {
            const char *p = path[0] == '/' ? path + 1 : path;
            if (strstr(p, "..")) {
                const char *forb = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send_all(client_sock, forb, strlen(forb)); write_log(client_ip, reqline, 403, 0); free(request); close(client_sock); return NULL;
            }
            snprintf(filepath, sizeof(filepath), "%s/%s", WWW_DIR, p);
        }
        int fd = open(filepath, O_RDONLY);
        if (fd == -1) {
            const char *notf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client_sock, notf, strlen(notf)); write_log(client_ip, reqline, 404, 0); free(request); close(client_sock); return NULL;
        }
        off_t sz = lseek(fd, 0, SEEK_END); lseek(fd, 0, SEEK_SET);
        const char *mime = "text/html";
        char header[256];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %jd\r\nConnection: close\r\n\r\n",
            mime, (intmax_t)sz);
        send_all(client_sock, header, header_len);
        ssize_t r; char filebuf[4096]; size_t sent_total = 0;
        while ((r = read(fd, filebuf, sizeof(filebuf))) > 0) {
            if (send_all(client_sock, filebuf, r) == -1) break;
            sent_total += r;
        }
        write_log(client_ip, reqline, 200, sent_total); close(fd); free(request); close(client_sock); return NULL;
    }

    const char *not_allowed = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send_all(client_sock, not_allowed, strlen(not_allowed)); write_log(client_ip, reqline, 405, 0);
    free(request); close(client_sock); return NULL;
}
