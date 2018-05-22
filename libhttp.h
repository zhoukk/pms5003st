/*
 * libhttp.h -- http library.
 *
 * Copyright (c) zhoukk <izhoukk@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _LIBHTTP_H_
#define _LIBHTTP_H_


#ifdef __cplusplus
extern "C" {
#endif

struct libhttp_buf {
    char *data;
    int size;
};

struct libhttp_url;
struct libhttp_request;
struct libhttp_response;

extern struct url_api {

    struct libhttp_url *(* set_schema)(struct libhttp_url *url, const char *schema);

    struct libhttp_url *(* set_host)(struct libhttp_url *url, const char *host);

    struct libhttp_url *(* set_port)(struct libhttp_url *url, int port);

    struct libhttp_url *(* set_path)(struct libhttp_url *url, const char *path);

    struct libhttp_url *(* set_param)(struct libhttp_url *url, const char *key, const char *val);

    struct libhttp_url *(* set_fragment)(struct libhttp_url *url, const char *fragment);

    struct libhttp_url *(* set_userinfo)(struct libhttp_url *url, const char *userinfo);

    const char *(* schema)(struct libhttp_url *url);

    const char *(* host)(struct libhttp_url *url);

    int (* port)(struct libhttp_url *url);

    const char *(* path)(struct libhttp_url *url);

    const char *(* param)(struct libhttp_url *url, const char *key);

    const char *(* fragment)(struct libhttp_url *url);

    const char *(* userinfo)(struct libhttp_url *url);

    int (* build)(struct libhttp_url *url, char *s);

    int (* parse)(struct libhttp_url *url, const char *s);

} url_api;

extern struct request_api {

    struct libhttp_request *(* create)(void);

    void (* destroy)(struct libhttp_request *req);

    struct libhttp_request *(* set_method)(struct libhttp_request *req, const char *method);

    struct libhttp_request *(* set_header)(struct libhttp_request *req, const char *field, const char *value);

    struct libhttp_request *(* set_body)(struct libhttp_request *req, struct libhttp_buf body);

    const char *(* method)(struct libhttp_request *req);

    struct libhttp_url *(* url)(struct libhttp_request *req);

    const char *(* header)(struct libhttp_request *req, const char *field);

    struct libhttp_buf (* body)(struct libhttp_request *req);

    struct libhttp_buf (* build)(struct libhttp_request *req);

    int (* parse)(struct libhttp_request *req, struct libhttp_buf buf);

} request_api;

extern struct response_api {

    struct libhttp_response *(* create)(void);

    void (* destroy)(struct libhttp_response *res);

    struct libhttp_response *(* set_status)(struct libhttp_response *res, int status);

    struct libhttp_response *(* set_header)(struct libhttp_response *res, const char *field, const char *value);

    struct libhttp_response *(* set_body)(struct libhttp_response *res, struct libhttp_buf body);

    int (* status)(struct libhttp_response *res);

    const char *(* header)(struct libhttp_response *res, const char *field);

    struct libhttp_buf (* body)(struct libhttp_response *res);

    struct libhttp_buf (* build)(struct libhttp_response *res);

    int (* parse)(struct libhttp_response *res, struct libhttp_buf buf);

} response_api;

#ifdef __cplusplus
}
#endif

#endif // _LIBHTTP_H_

#ifdef LIBHTTP_IMPLEMENTATION

/**
 * Implement
 */

#include "http_parser.h"

#include "base64.h"
#include "urlcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct libhttp_param {
    char *key;
    char *val;

    struct libhttp_param *next;
};

struct libhttp_url {
    char *schema;
    char *host;
    int port;
    char *path;
    char *fragment;
    char *userinfo;
    struct {
        struct libhttp_param *head;
        struct libhttp_param *tail;
    } params;
};

struct libhttp_header {
    char *field;
    char *value;

    struct libhttp_header *next;
};

struct libhttp_headers {
    struct libhttp_header *head;
    struct libhttp_header *tail;
};

struct libhttp_request {
    const char *method;
    struct libhttp_url url;
    struct libhttp_headers headers;
    struct libhttp_buf body;

    struct {
        int complete;
        http_parser parser;
        const char *field_at;
        size_t field_length;
    } p;
};

struct libhttp_response {
    int status;
    struct libhttp_headers headers;
    struct libhttp_buf body;

    struct {
        int complete;
        http_parser parser;
        const char *field_at;
        size_t field_length;
    } p;
};


static const char *
http_status_str(enum http_status s) {
    switch (s) {
#define XX(num, name, string) case HTTP_STATUS_##name: return #num " " #string;
        HTTP_STATUS_MAP(XX)
#undef XX
    default: return "<unknown>";
    }
}

static void
__param_clear(struct libhttp_url *url) {
    struct libhttp_param *p;

    p = url->params.head;
    while (p) {
        struct libhttp_param *next;

        next = p->next;
        if (p->key) free(p->key);
        if (p->val) free(p->val);
        free(p);
        p = next;
    }
    url->params.head = url->params.tail = 0;
}

static void
__url_clear(struct libhttp_url *url) {
    if (url->schema) free(url->schema);
    if (url->host) free(url->host);
    if (url->path) free(url->path);
    if (url->fragment) free(url->fragment);
    if (url->userinfo) free(url->userinfo);
    __param_clear(url);
    memset(url, 0, sizeof *url);
}

static void
__headers_set(struct libhttp_headers *headers, const char *field, const char *value) {
    struct libhttp_header *header;

    header = headers->head;
    while (header) {
        if (0 == strcasecmp(header->field, field)) {
            if (header->value) {
                free(header->value);
                header->value = 0;
            }
            if (value) header->value = strdup(value);
            return;
        }
        header = header->next;
    }

    if (!value) return;
    header = (struct libhttp_header *)malloc(sizeof *header);
    memset(header, 0, sizeof *header);

    header->field = strdup(field);
    header->value = strdup(value);
    if (headers->head == 0) {
        headers->head = headers->tail = header;
    } else {
        headers->tail->next = header;
        headers->tail = header;
    }
}

static const char *
__headers_get(struct libhttp_headers *headers, const char *field) {
    struct libhttp_header *header;

    header = headers->head;
    while (header) {
        if (0 == strcasecmp(header->field, field)) {
            return header->value;
        }
        header = header->next;
    }
    return 0;
}

static void
__headers_clear(struct libhttp_headers *headers) {
    struct libhttp_header *header;

    header = headers->head;
    while (header) {
        struct libhttp_header *next;

        next = header->next;
        if (header->field) free(header->field);
        if (header->value) free(header->value);
        free(header);
        header = next;
    }
    headers->head = headers->tail = 0;
}

struct libhttp_url *
libhttp_url__set_schema(struct libhttp_url *url, const char *schema) {
    if (url->schema) free(url->schema);
    url->schema = strdup(schema);
    return url;
}

struct libhttp_url *
libhttp_url__set_host(struct libhttp_url *url, const char *host) {
    if (url->host) free(url->host);
    url->host = strdup(host);
    return url;
}

struct libhttp_url *
libhttp_url__set_port(struct libhttp_url *url, int port) {
    url->port = port;
    return url;
}

struct libhttp_url *
libhttp_url__set_path(struct libhttp_url *url, const char *path) {
    if (url->path) free(url->path);
    url->path = strdup(path);
    return url;
}

static void
__param_parse(struct libhttp_url *url, const char *query) {
    char str[strlen(query) + 1];
    char *p, *arg;

    strcpy(str, query);
    p = arg = str;
    while (p && *p != '\0') {
        char *key, *val;
        struct libhttp_param *param;

        arg = strsep(&p, "&");
        val = arg;
        key = strsep(&val, "=");
        if (!val || *key == '\0') {
            break;
        }

        char deval[strlen(val)+1];
        url_decode(val, strlen(val), deval);
        param = (struct libhttp_param *)malloc(sizeof *param);
        memset(param, 0, sizeof *param);
        param->key = strdup(key);
        param->val = strdup(deval);
        if (!url->params.head) {
            url->params.head = url->params.tail = param;
        } else {
            url->params.tail->next = param;
            url->params.tail = param;
        }
    }
}

struct libhttp_url *
libhttp_url__set_param(struct libhttp_url *url, const char *key, const char *val) {
    struct libhttp_param *p;

    if (!key || !val || !key[0] || !val[0]) return 0;
    p = url->params.head;
    while (p) {
        if (0 == strcasecmp(p->key, key)) {
            if (p->val) free(p->val);
            p->val = strdup(val);
            return url;
        }
        p = p->next;
    }

    p = (struct libhttp_param *)malloc(sizeof *p);
    memset(p, 0, sizeof *p);
    p->key = strdup(key);
    p->val = strdup(val);
    if (!url->params.head) {
        url->params.head = url->params.tail = p;
    } else {
        url->params.tail->next = p;
        url->params.tail = p;
    }
    return url;
}

struct libhttp_url *
libhttp_url__set_fragment(struct libhttp_url *url, const char *fragment) {
    if (url->fragment) free(url->fragment);
    url->fragment = strdup(fragment);
    return url;
}

struct libhttp_url *
libhttp_url__set_userinfo(struct libhttp_url *url, const char *userinfo) {
    if (url->userinfo) free(url->userinfo);
    url->userinfo = strdup(userinfo);
    return url;
}

const char *
libhttp_url__schema(struct libhttp_url *url) {
    return url->schema ? url->schema : "http";
}

const char *
libhttp_url__host(struct libhttp_url *url) {
    return url->host;
}

int
libhttp_url__port(struct libhttp_url *url) {
    return url->port > 0 ? url->port : 80;
}

const char *
libhttp_url__path(struct libhttp_url *url) {
    return url->path;
}

const char *
libhttp_url__param(struct libhttp_url *url, const char *key) {
    struct libhttp_param *p;

    p = url->params.head;
    while (p) {
        if (0 == strcasecmp(p->key, key))
            return p->val;
        p = p->next;
    }
    return 0;
}

const char *
libhttp_url__fragment(struct libhttp_url *url) {
    return url->fragment;
}

const char *
libhttp_url__userinfo(struct libhttp_url *url) {
    return url->userinfo;
}

int
libhttp_url__build(struct libhttp_url *url, char *s) {
    struct libhttp_param *p;
    char *schema;
    char *path;
    int port;
    int len;

    if (url->schema)
        schema = url->schema;
    else
        schema = "http";
    if (url->port)
        port = url->port;
    else if (0 == strcmp(schema, "https"))
        port = 443;
    else
        port = 90;
    if (url->path)
        path = url->path;
    else
        path = "";

    len = sprintf(s, "%s://%s", schema, url->host);
    if ((port != 80 && strcmp(schema, "http")) || (port != 443 && strcmp(schema, "https"))) {
        char sport[6] = {0};

        sprintf(sport, "%d", port);
        strcat(s, ":");
        strcat(s, sport);
        len += 1 + strlen(sport);
    }
    strcat(s, path);
    len += strlen(path);
    p = url->params.head;
    if (p) {
        strcat(s, "?");
        len += 1;
        while (p) {
            strcat(s, p->key);
            strcat(s, "=");
            strcat(s, p->val);
            len += strlen(p->key) + 1 + strlen(p->val);
            if (p->next) {
                strcat(s, "&");
                len += 1;
            }
            p = p->next;
        }
    }
    if (url->fragment) {
        strcat(s, "#");
        strcat(s, url->fragment);
        len += 1 + strlen(url->fragment);
    }

    return len;
}

int
libhttp_url__parse(struct libhttp_url *url, const char *s) {
    struct http_parser_url u;
    int off, len, rc;

    http_parser_url_init(&u);
    rc = http_parser_parse_url(s, strlen(s), 0, &u);
    if (rc) return rc;

    if (u.field_set & (1 << UF_SCHEMA)) {
        off = u.field_data[UF_SCHEMA].off;
        len = u.field_data[UF_SCHEMA].len;
        url->schema = strndup(s + off, len);
    }
    if (u.field_set & (1 << UF_HOST)) {
        off = u.field_data[UF_HOST].off;
        len = u.field_data[UF_HOST].len;
        url->host = strndup(s + off, len);
    }
    if (u.field_set & (1 << UF_PORT)) {
        url->port = u.port;
    } else if (url->schema && 0 == strcasecmp(url->schema, "http")) {
        url->port = 80;
    } else if (url->schema && 0 == strcasecmp(url->schema, "https")) {
        url->port = 443;
    }
    if (u.field_set & (1 << UF_PATH)) {
        off = u.field_data[UF_PATH].off;
        len = u.field_data[UF_PATH].len;
        url->path = strndup(s + off, len);
    }
    if (u.field_set & (1 << UF_QUERY)) {
        off = u.field_data[UF_QUERY].off;
        len = u.field_data[UF_QUERY].len;
        char query[len + 1];
        strncpy(query, s + off, len);
        query[len] = '\0';
        __param_clear(url);
        __param_parse(url, query);
    }
    if (u.field_set & (1 << UF_FRAGMENT)) {
        off = u.field_data[UF_FRAGMENT].off;
        len = u.field_data[UF_FRAGMENT].len;
        url->fragment = strndup(s + off, len);
    }
    if (u.field_set & (1 << UF_USERINFO)) {
        off = u.field_data[UF_USERINFO].off;
        len = u.field_data[UF_USERINFO].len;
        url->userinfo = strndup(s + off, len);
    }
    return 0;
}


struct url_api url_api = {
    libhttp_url__set_schema,
    libhttp_url__set_host,
    libhttp_url__set_port,
    libhttp_url__set_path,
    libhttp_url__set_param,
    libhttp_url__set_fragment,
    libhttp_url__set_userinfo,
    libhttp_url__schema,
    libhttp_url__host,
    libhttp_url__port,
    libhttp_url__path,
    libhttp_url__param,
    libhttp_url__fragment,
    libhttp_url__userinfo,
    libhttp_url__build,
    libhttp_url__parse
};


struct libhttp_request *
libhttp_request__create(void) {
    struct libhttp_request *req;

    req = (struct libhttp_request *)malloc(sizeof *req);
    memset(req, 0, sizeof *req);

    http_parser_init(&req->p.parser, HTTP_REQUEST);
    req->p.parser.data = req;

    return req;
}

void
libhttp_request__destroy(struct libhttp_request *req) {
    __url_clear(&req->url);
    __headers_clear(&req->headers);
    if (req->body.data) free(req->body.data);
    free(req);
}

struct libhttp_request *
libhttp_request__set_method(struct libhttp_request *req, const char *method) {
    req->method = method;
    return req;
}

struct libhttp_request *
libhttp_request__set_header(struct libhttp_request *req, const char *field, const char *value) {
    __headers_set(&req->headers, field, value);
    return req;
}

struct libhttp_request *
libhttp_request__set_body(struct libhttp_request *req, struct libhttp_buf body) {
    if (req->body.data) free(req->body.data);
    if (body.size) {
        req->body.data = malloc(body.size);
        memcpy(req->body.data, body.data, body.size);
    }
    req->body.size = body.size;
    return req;
}

const char *
libhttp_request__method(struct libhttp_request *req) {
    return req->method;
}

struct libhttp_url *
libhttp_request__url(struct libhttp_request *req) {
    return &req->url;
}

struct libhttp_buf
libhttp_request__body(struct libhttp_request *req) {
    return req->body;
}

const char *
libhttp_request__header(struct libhttp_request *req, const char *field) {
    return __headers_get(&req->headers, field);
}

struct libhttp_buf
libhttp_request__build(struct libhttp_request *req) {
    struct libhttp_buf buf;
    struct libhttp_param *p;
    struct libhttp_header *header;
    char url[4096] = {0};
    int guess_size = 4200;
    int has_content_length = 0;
    int size;
    char *data;

    strcat(url, req->url.path);
    p = req->url.params.head;
    if (p) {
        strcat(url, "?");
        while (p) {
            char encval[3 * strlen(p->val) + 1];

            url_encode(p->val, strlen(p->val), encval);
            strcat(url, p->key);
            strcat(url, "=");
            strcat(url, encval);
            if (p->next) {
                strcat(url, "&");
            }
            p = p->next;
        }
    }
    if (req->url.fragment) {
        strcat(url, "#");
        strcat(url, req->url.fragment);
    }

    if (req->url.userinfo) {
        char auth[2 * strlen(req->url.userinfo) + 10];
        strcpy(auth, "Basic ");
        base64_encode(req->url.userinfo, strlen(req->url.userinfo), auth+6);
        __headers_set(&req->headers, "Authorization", auth);
    }

    header = req->headers.head;
    while (header) {
        if (header->value)
            guess_size += 2 + strlen(header->field) + strlen(header->value);
        header = header->next;
    }
    guess_size += 4 + req->body.size;

    data = malloc(guess_size);
    size = snprintf(data, guess_size, "%s %s HTTP/1.1\r\n", req->method, url);

    header = req->headers.head;
    while (header) {
        if (header->value) {
            size += snprintf(data + size, guess_size - size, "%s:%s\r\n", header->field, header->value);
            if (0 == strcasecmp(header->field, "Content-Length")) {
                has_content_length = 1;
            }
        }
        header = header->next;
    }
    if (has_content_length == 0)
        size += snprintf(data + size, guess_size - size, "Content-Length:%d\r\n", req->body.size);
    strcat(data + size, "\r\n");
    size += 2;
    if (req->body.size && req->body.data) {
        memcpy(data + size, req->body.data, req->body.size);
        size += req->body.size;
    }
    buf.size = size;
    buf.data = data;
    return buf;
}

static int
__on_request_message_begin(http_parser *p) {
    struct libhttp_request *req = (struct libhttp_request *)p->data;
    __headers_clear(&req->headers);
    req->p.complete = 0;
    return 0;
}

static int
__on_request_url(http_parser *p, const char *at, size_t length) {
    struct libhttp_request *req = (struct libhttp_request *)p->data;
    char url[length+1];
    strncpy(url, at, length);
    url[length] = '\0';
    libhttp_url__parse(&req->url, url);
    return 0;
}

static int
__on_request_header_field(http_parser *p, const char *at, size_t length) {
    struct libhttp_request *req = (struct libhttp_request *)p->data;
    req->p.field_at = at;
    req->p.field_length = length;
    return 0;
}

static int
__on_request_header_value(http_parser *p, const char *at, size_t length) {
    struct libhttp_request *req = (struct libhttp_request *)p->data;
    char field[req->p.field_length+1];
    char value[length+1];
    strncpy(field, req->p.field_at, req->p.field_length);
    field[req->p.field_length] = '\0';
    strncpy(value, at, length);
    value[length] = '\0';
    __headers_set(&req->headers, field, value);
    if (0 == strcmp(field, "Authorization") && 0 == strncmp(value, "Basic ", 6)) {
        char userinfo[length];
        base64_decode(value+6, length-6, userinfo);
        if (req->url.userinfo) free(req->url.userinfo);
        req->url.userinfo = strdup(userinfo);
    }
    return 0;
}

static int
__on_request_body(http_parser *p, const char *at, size_t length) {
    struct libhttp_request *req = (struct libhttp_request *)p->data;
    char *body = malloc(req->body.size + length);
    if (req->body.data) {
        memcpy(body, req->body.data, req->body.size);
        free(req->body.data);
    }
    memcpy(body + req->body.size, at, length);
    req->body.data = body;
    req->body.size += length;
    return 0;
}

static int
__on_request_message_complete(http_parser *p) {
    struct libhttp_request *req = (struct libhttp_request *)p->data;
    req->method = http_method_str(req->p.parser.method);
    req->p.complete = 1;
    return 0;
}

int
libhttp_request__parse(struct libhttp_request *req, struct libhttp_buf buf) {
    static http_parser_settings settings = {
        .on_message_begin = __on_request_message_begin,
        .on_url = __on_request_url,
        .on_header_field = __on_request_header_field,
        .on_header_value = __on_request_header_value,
        .on_body = __on_request_body,
        .on_message_complete = __on_request_message_complete
    };

    int parsed = http_parser_execute(&req->p.parser, &settings, buf.data, buf.size);
    if (req->p.parser.http_errno) {
        fprintf(stderr, "http_parser_execute: %s %s\n", http_errno_name(req->p.parser.http_errno), http_errno_description(req->p.parser.http_errno));
        return -1;
    }
    if (parsed < buf.size) {
        fprintf(stderr, "http_parse_execute size:%d, parsed:%d\n", buf.size, parsed);
        return -1;
    }
    return req->p.complete;
}

struct request_api request_api = {
    libhttp_request__create,
    libhttp_request__destroy,
    libhttp_request__set_method,
    libhttp_request__set_header,
    libhttp_request__set_body,
    libhttp_request__method,
    libhttp_request__url,
    libhttp_request__header,
    libhttp_request__body,
    libhttp_request__build,
    libhttp_request__parse
};


struct libhttp_response *
libhttp_response__create(void) {
    struct libhttp_response *res;

    res = (struct libhttp_response *)malloc(sizeof *res);
    memset(res, 0, sizeof *res);

    http_parser_init(&res->p.parser, HTTP_RESPONSE);
    res->p.parser.data = res;

    return res;
}

void
libhttp_response__destroy(struct libhttp_response *res) {
    __headers_clear(&res->headers);
    if (res->body.data) free(res->body.data);
    free(res);
}

struct libhttp_response *
libhttp_response__set_status(struct libhttp_response *res, int status) {
    res->status = status;
    return res;
}

struct libhttp_response *
libhttp_response__set_header(struct libhttp_response *res, const char *field, const char *value) {
    __headers_set(&res->headers, field, value);
    return res;
}

struct libhttp_response *
libhttp_response__set_body(struct libhttp_response *res, struct libhttp_buf body) {
    if (res->body.data) free(res->body.data);
    if (body.size) {
        res->body.data = malloc(body.size);
        memcpy(res->body.data, body.data, body.size);
    }
    res->body.size = body.size;
    return res;
}

int
libhttp_response__status(struct libhttp_response *res) {
    return res->status;
}

const char *
libhttp_response__header(struct libhttp_response *res, const char *field) {
    return __headers_get(&res->headers, field);
}

struct libhttp_buf
libhttp_response__body(struct libhttp_response *res) {
    return res->body;
}

struct libhttp_buf
libhttp_response__build(struct libhttp_response *res) {
    struct libhttp_buf buf;
    struct libhttp_header *header;
    int guess_size = 100;
    int has_content_length = 0;
    int size;
    char *data;

    header = res->headers.head;
    while (header) {
        if (header->value)
            guess_size += 2 + strlen(header->field) + strlen(header->value);
        header = header->next;
    }
    guess_size += 4 + res->body.size;

    data = malloc(guess_size);
    size = snprintf(data, guess_size, "HTTP/1.1 %s\r\n", http_status_str(res->status));

    header = res->headers.head;
    while (header) {
        if (header->value) {
            size += snprintf(data + size, guess_size - size, "%s:%s\r\n", header->field, header->value);
            if (0 == strcasecmp(header->field, "Content-Length")) {
                has_content_length = 1;
            }
        }
        header = header->next;
    }
    if (has_content_length == 0)
        size += snprintf(data + size, guess_size - size, "Content-Length:%d\r\n", res->body.size);
    strcat(data + size, "\r\n");
    size += 2;
    if (res->body.size && res->body.data) {
        memcpy(data + size, res->body.data, res->body.size);
        size += res->body.size;
    }
    buf.size = size;
    buf.data = data;
    return buf;
}

static int
__on_response_message_begin(http_parser *p) {
    struct libhttp_response *res = (struct libhttp_response *)p->data;
    __headers_clear(&res->headers);
    res->p.complete = 0;
    return 0;
}

static int
__on_response_header_field(http_parser *p, const char *at, size_t length) {
    struct libhttp_response *res = (struct libhttp_response *)p->data;
    res->p.field_at = at;
    res->p.field_length = length;
    return 0;
}

static int
__on_response_header_value(http_parser *p, const char *at, size_t length) {
    struct libhttp_response *res = (struct libhttp_response *)p->data;
    char field[res->p.field_length+1];
    char value[length+1];
    strncpy(field, res->p.field_at, res->p.field_length);
    field[res->p.field_length] = '\0';
    strncpy(value, at, length);
    value[length] = '\0';
    __headers_set(&res->headers, field, value);
    return 0;
}

static int
__on_response_body(http_parser *p, const char *at, size_t length) {
    struct libhttp_response *res = (struct libhttp_response *)p->data;
    char *body = malloc(res->body.size + length);
    if (res->body.data) {
        memcpy(body, res->body.data, res->body.size);
        free(res->body.data);
    }
    memcpy(body + res->body.size, at, length);
    res->body.data = body;
    res->body.size += length;
    return 0;
}

static int
__on_response_message_complete(http_parser *p) {
    struct libhttp_response *res = (struct libhttp_response *)p->data;
    res->status = res->p.parser.status_code;
    res->p.complete = 1;
    return 0;
}

int
libhttp_response__parse(struct libhttp_response *res, struct libhttp_buf buf) {
    static http_parser_settings settings = {
        .on_message_begin = __on_response_message_begin,
        .on_header_field = __on_response_header_field,
        .on_header_value = __on_response_header_value,
        .on_body = __on_response_body,
        .on_message_complete = __on_response_message_complete
    };

    int parsed = http_parser_execute(&res->p.parser, &settings, buf.data, buf.size);
    if (res->p.parser.http_errno) {
        fprintf(stderr, "http_parser_execute: %s %s\n", http_errno_name(res->p.parser.http_errno), http_errno_description(res->p.parser.http_errno));
        return -1;
    }
    if (parsed < buf.size) {
        fprintf(stderr, "http_parse_execute size:%d, parsed:%d\n", buf.size, parsed);
        return -1;
    }
    return res->p.complete;
}

struct response_api response_api = {
    libhttp_response__create,
    libhttp_response__destroy,
    libhttp_response__set_status,
    libhttp_response__set_header,
    libhttp_response__set_body,
    libhttp_response__status,
    libhttp_response__header,
    libhttp_response__body,
    libhttp_response__build,
    libhttp_response__parse
};

#endif /* LIBHTTP_IMPLEMENTATION */
