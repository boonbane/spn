#ifndef SPN_EXTERNAL_NET_H
#define SPN_EXTERNAL_NET_H

#include "sp.h"
#include "spn/types.h"

#define spn_net_default_timeout_s 30
#define spn_net_max_timeout_s 65535
#define spn_net_max_retries 3

typedef struct {
  u32 wait_ms [spn_net_max_retries];
} spn_net_backoff_t;

#define spn_net_default_backoff ((spn_net_backoff_t) { .wait_ms = { 1000, 3500, 6500 } })
#define spn_net_no_backoff sp_zero_s(spn_net_backoff_t)

typedef struct {
  u32 timeout_s;
  sp_str_t ca_bundle;
  bool system_git;
} spn_net_config_t;

spn_net_trust_t spn_net_trust_find(sp_str_t ca_bundle, sp_str_t ssl_cert_file);
bool spn_net_backoff_wait(spn_net_backoff_t backoff, u32 retry, sp_str_t url, sp_str_t cause, sp_atomic_s32_t* cancelled);

#endif
