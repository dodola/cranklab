#include "pd_http_backend.h"
#include <string.h>
#include <stdio.h>
/* Hosts without a transport (the libretro core) report a clean failure; Lua sees
   a connection error and the game continues offline. */
int pd_http_fetch(const PDHttpRequest *request, PDHttpResponse *response) {
    (void)request;
    memset(response, 0, sizeof(*response));
    snprintf(response->error, sizeof(response->error), "Network is unavailable in this host");
    return 0;
}
