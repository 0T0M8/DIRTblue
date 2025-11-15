// util.c
#define _GNU_SOURCE
#include "server.h"

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

void write_log(const char *client_ip, const char *req_line, int status_code, size_t bytes_sent) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen(LOGFILE, "a");
    if (f) {
        fprintf(f, "%s - \"%s\" %d %zu\n", client_ip, req_line ? req_line : "-", status_code, bytes_sent);
        fclose(f);
    }
    pthread_mutex_unlock(&log_lock);
}

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

int parse_form(const char *body, char *name_out, size_t name_sz, int *age_out) {
    if (!body) return -1;
    char *buf = strdup(body);
    if (!buf) return -1;
    char *token = strtok(buf, "&");
    name_out[0] = '\0';
    *age_out = -1;
    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char *k = token;
            char *v = eq + 1;
            if (strcmp(k, "name") == 0) {
                strncpy(name_out, v, name_sz-1);
                name_out[name_sz-1] = '\0';
                url_decode_inplace(name_out);
            } else if (strcmp(k, "age") == 0) {
                char tmp[32];
                strncpy(tmp, v, sizeof(tmp)-1);
                tmp[sizeof(tmp)-1] = '\0';
                url_decode_inplace(tmp);
                *age_out = atoi(tmp);
            }
        }
        token = strtok(NULL, "&");
    }
    free(buf);
    if (name_out[0] == '\0' || *age_out < 0) return -1;
    return 0;
}

char *find_header_value(const char *headers, const char *name) {
    if (!headers || !name) return NULL;
    const char *p = headers;
    size_t namelen = strlen(name);
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
                strncpy(out, val, vlen);
                out[vlen] = '\0';
                return out;
            }
        }
        if (!line_end) break;
        p = line_end + 2;
    }
    return NULL;
}

/* receive headers and body until full request (Content-Length aware) */
ssize_t recv_full_request(int sock, char **out_buf) {
    size_t cap = BUF_CHUNK, len = 0;
    char *buf = malloc(cap);
    if (!buf) return -1;
    buf[0] = '\0';
    ssize_t n;
    char tmp[BUF_CHUNK];
    int headers_found = 0;
    int content_length = 0;
    size_t needed = 0;

    while (1) {
        n = recv(sock, tmp, sizeof(tmp), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return -1;
        } else if (n == 0) {
            break;
        }
        if (len + (size_t)n + 1 > cap) {
            cap = (len + (size_t)n + 1) * 2;
            char *t = realloc(buf, cap);
            if (!t) { free(buf); return -1; }
            buf = t;
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
