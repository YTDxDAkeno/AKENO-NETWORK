/* Internal glue between the PS5 platform helpers (transport_ps5.c) and the libSceHttp2 backend. */
#ifndef AKENO_PS5_PLATFORM_H
#define AKENO_PS5_PLATFORM_H

#include "akeno/akeno.h"

/* Initializes libSceNet + libSceSsl + libSceHttp2 once. Safe to call repeatedly. */
akeno_status akeno_ps5_http_global_init(void);
void akeno_ps5_http_global_fini(void);

#endif
