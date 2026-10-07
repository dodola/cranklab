#pragma once
#include <stddef.h>
typedef struct {
    const char *server, *path, *method, *headers;
    const char *body;
    size_t body_size;
    int port, ssl, timeout_ms;
} PDHttpRequest;
typedef struct {
    unsigned char *body;
    size_t size;
    int status;
    char *headers;
    char error[256];
} PDHttpResponse;
int pd_http_fetch(const PDHttpRequest *request, PDHttpResponse *response);
