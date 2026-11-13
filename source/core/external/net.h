#ifndef SPN_EXTERNAL_NET_H
#define SPN_EXTERNAL_NET_H

#include "sp.h"
#include "spn/types.h"

#define spn_net_default_timeout_s 30
#define spn_net_max_timeout_s 65535
#define spn_net_attempts 4

typedef struct {
  u32 timeout_s;
  sp_str_t ca_bundle;
} spn_net_config_t;

spn_net_trust_t spn_net_trust_find(sp_str_t ca_bundle, sp_str_t ssl_cert_file);

#endif
