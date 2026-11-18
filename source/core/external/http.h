#ifndef SPN_EXTERNAL_HTTP_H
#define SPN_EXTERNAL_HTTP_H

#include "sp.h"
#include "spn/core.h"
#include "external/net.h"

#define spn_http_message_max 256

typedef struct {
  sp_str_t ca_bundle;
  u32 timeout_s;
  sp_atomic_s32_t* cancelled;
} spn_http_config_t;

typedef enum {
  SPN_HTTP_FAILED,
  SPN_HTTP_DROPPED,
  SPN_HTTP_CANCELLED,
  SPN_HTTP_UNTRUSTED,
  SPN_HTTP_UNWRITABLE,
} spn_http_failure_kind_t;

typedef struct {
  spn_http_failure_kind_t kind;
  c8 message [spn_http_message_max];
} spn_http_failure_t;

typedef struct {
  spn_err_t (*download)(void* user, const spn_http_config_t* config, sp_str_t url, sp_path_t dest, spn_http_failure_t* failure);
  void* user;
} spn_http_t;

spn_err_t spn_http_init();
spn_err_t spn_http_download(void* user, const spn_http_config_t* config, sp_str_t url, sp_path_t dest, spn_http_failure_t* failure);

#endif
