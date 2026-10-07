#include "pd_http_backend.h"
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wchar_t *wide(const char *text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *result = calloc(n ? n : 1, sizeof(wchar_t));
    if (result && n) MultiByteToWideChar(CP_UTF8, 0, text, -1, result, n);
    return result;
}
int pd_http_fetch(const PDHttpRequest *r, PDHttpResponse *out) {
    HINTERNET session = NULL, connect = NULL, request = NULL;
    wchar_t *server = wide(r->server), *path = wide(r->path), *method = wide(r->method);
    wchar_t *headers = wide(r->headers ? r->headers : "");
    memset(out, 0, sizeof(*out));
    if (!server || !path || !method || !headers) goto failed;
    session = WinHttpOpen(L"CrankRuntime/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!session) goto failed;
    WinHttpSetTimeouts(session, r->timeout_ms, r->timeout_ms, r->timeout_ms, r->timeout_ms);
    connect = WinHttpConnect(session, server, (INTERNET_PORT)r->port, 0);
    if (!connect) goto failed;
    request = WinHttpOpenRequest(connect, method, path, NULL, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, r->ssl ? WINHTTP_FLAG_SECURE : 0);
    if (!request) goto failed;
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
    if (!WinHttpSendRequest(request, headers, (DWORD)-1, (void *)r->body,
                           (DWORD)r->body_size, (DWORD)r->body_size, 0) ||
        !WinHttpReceiveResponse(request, NULL)) goto failed;
    DWORD status = 0, bytes = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        NULL, &status, &bytes, NULL);
    out->status = (int)status;
    bytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL, NULL, &bytes, NULL);
    if (bytes) {
        wchar_t *raw = calloc(1, bytes + sizeof(wchar_t));
        if (raw && WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL, raw, &bytes, NULL)) {
            int count = WideCharToMultiByte(CP_UTF8, 0, raw, -1, NULL, 0, NULL, NULL);
            out->headers = calloc(1, count);
            if (out->headers) WideCharToMultiByte(CP_UTF8, 0, raw, -1, out->headers, count, NULL, NULL);
        }
        free(raw);
    }
    for (;;) {
        DWORD available = 0, read = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) goto failed;
        if (!available) break;
        if (out->size + available > 32 * 1024 * 1024) {
            snprintf(out->error, sizeof(out->error), "HTTP response exceeds 32 MB limit");
            goto cleanup;
        }
        unsigned char *next = realloc(out->body, out->size + available);
        if (!next) goto failed;
        out->body = next;
        if (!WinHttpReadData(request, out->body + out->size, available, &read)) goto failed;
        out->size += read;
    }
    goto cleanup;
failed:
    snprintf(out->error, sizeof(out->error), "HTTP transport error %lu", GetLastError());
cleanup:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    free(server); free(path); free(method); free(headers);
    return out->error[0] ? -1 : 0;
}
