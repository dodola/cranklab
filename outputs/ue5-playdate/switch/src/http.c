#include "pd_http_backend.h"
#include <curl/curl.h>
#include <stdint.h>
#include "pd_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned char *data;
  size_t size, limit;
} Buffer;
static size_t append(void *data, size_t width, size_t count, void *context) {
  Buffer *b = context;
  if (width && count > SIZE_MAX / width)
    return 0;
  size_t n = width * count;
  if (n > b->limit - b->size)
    return 0;
  void *next = realloc(b->data, b->size + n + 1);
  if (!next)
    return 0;
  b->data = next;
  memcpy(b->data + b->size, data, n);
  b->size += n;
  b->data[b->size] = 0;
  return n;
}
static int cancelled(void *unused,curl_off_t total,curl_off_t received,curl_off_t sentTotal,curl_off_t sent) {
  (void)unused;(void)total;(void)received;(void)sentTotal;(void)sent;
  return pd_host_should_stop();
}
int pd_http_fetch(const PDHttpRequest *r, PDHttpResponse *out) {
  memset(out, 0, sizeof(*out));
  CURL *curl = curl_easy_init();
  if (!curl) {
    snprintf(out->error, sizeof(out->error), "Cannot initialize HTTP");
    return -1;
  }
  char url[4096];
  int n = snprintf(url, sizeof(url), "%s://%s:%d%s", r->ssl ? "https" : "http",
                   r->server, r->port, r->path);
  if (n < 0 || (size_t)n >= sizeof(url)) {
    curl_easy_cleanup(curl);
    snprintf(out->error, sizeof(out->error), "URL is too long");
    return -1;
  }
  Buffer body = {NULL, 0, 32 * 1024 * 1024}, headers = {NULL, 0, 64 * 1024};
  struct curl_slist *list = NULL;
  char *raw = strdup(r->headers ? r->headers : "");
  if (raw) {
    char *save = NULL;
    for (char *h = strtok_r(raw, "\r\n", &save); h;
         h = strtok_r(NULL, "\r\n", &save))
      list = curl_slist_append(list, h);
  }
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, r->method);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  if (r->body_size) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, r->body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                     (curl_off_t)r->body_size);
  }
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   (long)(r->timeout_ms > 0 ? r->timeout_ms : 30000));
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP|CURLPROTO_HTTPS);
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP|CURLPROTO_HTTPS);
#endif
  curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
  curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,cancelled);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "PlaydateSwitch/0.1");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, append);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
#ifdef __SWITCH__
  curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
#endif
  CURLcode result = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  out->status = (int)status;
  out->body = body.data;
  out->size = body.size;
  out->headers = (char *)headers.data;
  if (result != CURLE_OK)
    snprintf(out->error, sizeof(out->error), "HTTP: %s",
             curl_easy_strerror(result));
  curl_slist_free_all(list);
  free(raw);
  curl_easy_cleanup(curl);
  return result == CURLE_OK ? 0 : -1;
}
