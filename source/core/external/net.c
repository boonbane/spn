#include "sp.h"
#include "macro/macro.h"
#include "net.h"

#include "ctx/types.h"
#include "event/event.h"

#define spn_net_wait_slice_ms 20

void spn_net_wait(void* user, u32 ms, sp_atomic_s32_t* cancelled) {
  u64 wait = sp_tm_ms_to_ns(ms);
  u64 elapsed = 0;
  sp_tm_timer_t timer = sp_tm_start_timer();
  while (elapsed < wait && !sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    sp_os_sleep_ns(sp_min(wait - elapsed, sp_tm_ms_to_ns(spn_net_wait_slice_ms)));
    elapsed = sp_tm_read_timer(&timer);
  }
}

bool spn_net_backoff_wait(spn_net_backoff_t backoff, u32 retry, sp_str_t url, sp_str_t cause, sp_atomic_s32_t* cancelled) {
  if (retry >= spn_net_max_retries || !backoff.wait_ms[retry]) {
    return false;
  }
  sp_assert(backoff.wait);

  u32 last = retry;
  while (last < spn_net_max_retries && backoff.wait_ms[last]) {
    last++;
  }
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SYNC_RETRY,
    .sync_retry = {
      .url = sp_str_copy(spn.events->mem, url),
      .cause = sp_str_copy(spn.events->mem, cause),
      .remaining = last - retry,
    },
  });

  backoff.wait(backoff.user, backoff.wait_ms[retry], cancelled);
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
  sp_sys_file_meta_t meta = sp_zero;
  if (!sp_str_empty(ssl_cert_file)) {
    bool missing = sp_fs_get_path_metadata(ssl_cert_file, &meta) || meta.kind != SP_FS_KIND_FILE;
    return (spn_net_trust_t) { .kind = SPN_NET_TRUST_ENV, .bundle = ssl_cert_file, .missing = missing };
  }
  if (!sp_str_empty(ca_bundle)) {
    bool missing = sp_fs_get_path_metadata(ca_bundle, &meta) || meta.kind != SP_FS_KIND_FILE;
    return (spn_net_trust_t) { .kind = SPN_NET_TRUST_CONFIG, .bundle = ca_bundle, .missing = missing };
  }
  sp_carr_for(bundles, it) {
    sp_str_t bundle = sp_cstr_as_str(bundles[it]);
    if (!sp_fs_get_path_metadata(bundle, &meta) && meta.kind == SP_FS_KIND_FILE) {
      return (spn_net_trust_t) { .kind = SPN_NET_TRUST_FOUND, .bundle = bundle };
    }
  }
  return (spn_net_trust_t) { .kind = SPN_NET_TRUST_NONE };
}
#endif
