#include "sp.h"
#include "macro/macro.h"
#include "net.h"

#if defined(SP_WIN32)
spn_net_trust_t spn_net_trust_find(sp_str_t ssl_cert_file) {
  return (spn_net_trust_t) { .kind = SPN_NET_TRUST_SYSTEM };
}
#else
static const c8* bundles [] = {
  "/etc/ssl/certs/ca-certificates.crt",
  "/etc/pki/tls/certs/ca-bundle.crt",
  "/etc/ssl/ca-bundle.pem",
  "/etc/pki/tls/cacert.pem",
  "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
  "/etc/ssl/cert.pem",
};

spn_net_trust_t spn_net_trust_find(sp_str_t ssl_cert_file) {
  if (sp_fs_is_target_file(ssl_cert_file)) {
    return (spn_net_trust_t) { .kind = SPN_NET_TRUST_BUNDLE, .bundle = ssl_cert_file };
  }
  sp_carr_for(bundles, it) {
    sp_str_t bundle = sp_cstr_as_str(bundles[it]);
    if (sp_fs_is_target_file(bundle)) {
      return (spn_net_trust_t) { .kind = SPN_NET_TRUST_BUNDLE, .bundle = bundle };
    }
  }
  return (spn_net_trust_t) { .kind = SPN_NET_TRUST_NONE };
}
#endif
