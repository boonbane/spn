#ifndef SPN_EXTERNAL_NET_H
#define SPN_EXTERNAL_NET_H

#include "sp.h"

#define spn_net_timeout_s 30
#define spn_net_attempts 4

typedef enum {
  SPN_NET_TRUST_NONE,
  SPN_NET_TRUST_BUNDLE,
  SPN_NET_TRUST_SYSTEM,
} spn_net_trust_kind_t;

typedef struct {
  spn_net_trust_kind_t kind;
  sp_str_t bundle;
} spn_net_trust_t;

spn_net_trust_t spn_net_trust_find(sp_str_t ssl_cert_file);

#endif
