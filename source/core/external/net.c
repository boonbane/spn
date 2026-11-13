#include "sp.h"
#include "macro/macro.h"
#include "net.h"

#define spn_net_wait_slice_ms 20

bool spn_net_backoff_wait(spn_net_backoff_t backoff, u32 retry, sp_atomic_s32_t* cancelled) {
  if (retry >= spn_net_max_retries || !backoff.wait_ms[retry]) {
    return false;
  }

  u64 wait = sp_tm_ms_to_ns(backoff.wait_ms[retry]);
  u64 elapsed = 0;
  sp_tm_timer_t timer = sp_tm_start_timer();
  while (elapsed < wait && !sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    sp_os_sleep_ns(sp_min(wait - elapsed, sp_tm_ms_to_ns(spn_net_wait_slice_ms)));
    elapsed = sp_tm_read_timer(&timer);
  }
  return true;
}

#if defined(SP_WIN32)
spn_net_trust_t spn_net_trust_find(sp_str_t ca_bundle, sp_str_t ssl_cert_file) {
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

spn_net_trust_t spn_net_trust_find(sp_str_t ca_bundle, sp_str_t ssl_cert_file) {
  if (!sp_str_empty(ssl_cert_file)) {
    return (spn_net_trust_t) { .kind = SPN_NET_TRUST_ENV, .bundle = ssl_cert_file, .missing = !sp_fs_is_target_file(ssl_cert_file) };
  }
  if (!sp_str_empty(ca_bundle)) {
    return (spn_net_trust_t) { .kind = SPN_NET_TRUST_CONFIG, .bundle = ca_bundle, .missing = !sp_fs_is_target_file(ca_bundle) };
  }
  sp_carr_for(bundles, it) {
    sp_str_t bundle = sp_cstr_as_str(bundles[it]);
    if (sp_fs_is_target_file(bundle)) {
      return (spn_net_trust_t) { .kind = SPN_NET_TRUST_FOUND, .bundle = bundle };
    }
  }
  return (spn_net_trust_t) { .kind = SPN_NET_TRUST_NONE };
}
#endif
