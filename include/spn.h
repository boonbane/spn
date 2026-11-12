#ifndef SPN_SPN_H
#define SPN_SPN_H

#ifdef SPN_HOST_H
  #error "include/spn.h is the guest build script API; it cannot share a translation unit with spn/host.h"
#endif

#include "spn/guest.h"

#ifdef __wasm32__
  #define SPN_API __attribute__((import_module("env")))
  #define SPN_EXPORT __attribute__((used, visibility("default")))
#else
  #define SPN_API
  #define SPN_EXPORT
#endif

typedef struct spn              spn_t;
typedef struct spn_config       spn_config_t;
typedef struct spn_profile      spn_profile_t;
typedef struct spn_node         spn_node_t;

typedef spn_err_t (*spn_configure_fn_t) (spn_t*, spn_config_t*);
typedef spn_err_t (*spn_node_fn_t)      (spn_t*);

spn_err_t     spn_log(spn_t* spn, const c8* message);

spn_err_t     spn_get_root_manifest(spn_t* spn, spn_root_t* root);
spn_err_t     spn_get_root_source(spn_t* spn, spn_root_t* root);
spn_err_t     spn_get_root_work(spn_t* spn, spn_root_t* root);

spn_err_t     spn_io_write(spn_root_t root, const c8* sub, const c8* contents);
const c8*     spn_fmt_ex(const c8* fmt, const c8* a0, const c8* a1, const c8* a2, const c8* a3);

#define spn_fmt_select(fmt, a0, a1, a2, a3, ...) spn_fmt_ex(fmt, a0, a1, a2, a3)
#define spn_fmt(fmt, ...) spn_fmt_select(fmt, ##__VA_ARGS__, 0, 0, 0, 0, 0)

spn_node_t*   spn_add_node(spn_config_t* config, const c8* tag);
spn_err_t     spn_node_add_output(spn_node_t* node, spn_root_t root, const c8* sub);
spn_err_t     spn_node_add_output_dir(spn_node_t* node, spn_root_t root, const c8* sub);
spn_err_t     spn_node_link(spn_node_t* from, spn_node_t* to);
spn_err_t     spn_node_set_fn(spn_node_t* node, const c8* fn);

spn_profile_t* spn_get_profile(spn_t* spn);
spn_err_t      spn_profile_get_os(spn_profile_t* profile, spn_os_t* os);
spn_err_t      spn_profile_get_mode(spn_profile_t* profile, spn_mode_t* mode);
spn_err_t      spn_profile_get_opt(spn_profile_t* profile, spn_opt_level_t* opt);
spn_err_t      spn_profile_get_sanitizers(spn_profile_t* profile, spn_sanitizer_set_t* sanitizers);

#endif
