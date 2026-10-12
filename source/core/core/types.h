#ifndef SPN_CORE_TYPES_H
#define SPN_CORE_TYPES_H

#include "sp.h"
#include "spn/core.h"
#include "spn/types.h"

typedef struct spn_project_t spn_project_t;
typedef struct spn_ctx_t spn_ctx_t;
typedef struct spn_event_buffer_t spn_event_buffer_t;
typedef struct spn_index_info spn_index_info_t;
typedef struct spn_pkg_info spn_pkg_info_t;
typedef struct spn_resolved_pkg spn_resolved_pkg_t;
typedef struct spn_profile_info spn_profile_info_t;
typedef struct spn_target_info spn_target_info_t;
typedef struct spn_build_unit_t spn_build_unit_t;
typedef struct spn_pkg_unit_t spn_pkg_unit_t;
typedef struct spn_toolchain_unit_t spn_toolchain_unit_t;
typedef struct spn_toolchain_catalog_t spn_toolchain_catalog_t;
typedef struct spn_target_unit spn_target_unit_t;
typedef struct spn_session_t spn_session_t;
typedef struct spn_op_t spn_op_t;
typedef struct spn_user_node_t spn_user_node_t;
typedef struct spn_dag_build_t spn_dag_build_t;
typedef struct sp_intern_t sp_intern_t;

typedef sp_hash_t spn_build_id_t;

typedef struct {
  spn_wake_fn_t fn;
  void* data;
  sp_atomic_u32_t signaled;
} spn_wake_t;

typedef struct toml_table_t toml_table_t;

#if defined(SP_CPP)
  #if defined(_MSVC_LANG)
    #define SP_CPP_VERSION _MSVC_LANG
  #else
    #define SP_CPP_VERSION __cplusplus
  #endif
#endif

#if defined(SP_CPP) && SP_CPP_VERSION >= 201103L
  template<typename T> struct sp_typeof_t      { typedef T type; };
  template<typename T> struct sp_typeof_t<T&>  { typedef T type; };
  template<typename T> struct sp_typeof_t<T&&> { typedef T type; };
  #define SP_HAS_TYPEOF 1
  #define sp_type_of(x) typename sp_typeof_t<decltype(x)>::type
#elif defined(__GNUC__) || defined(__clang__) || defined(__TINYC__)
  #define SP_HAS_TYPEOF 1
  #define sp_type_of(x) __typeof__(x)
#elif defined(_MSC_VER) && !defined(SP_CPP) && _MSC_VER >= 1939
  #define SP_HAS_TYPEOF 1
  #define sp_type_of(x) __typeof__(x)
#elif !defined(SP_CPP) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define SP_HAS_TYPEOF 1
  #define sp_type_of(x) typeof(x)
#else
  #define SP_HAS_TYPEOF 0
  #define sp_type_of(x) sp_type_of_is_unsupported_on_this_toolchain
#endif

#define sp_da_value_t(arr) sp_type_of(arr[0])

#define sp_da_for_ptr(arr, it) \
  for (__typeof__(arr) it = (arr); (it - (arr)) < sp_da_size(arr); it++)

#define __sp_it(it) sp_mcat(it, _p)
#define __sp_it_end(it) sp_mcat(it, _end)

#define sp_da_for_value(__arr, __it) \
  for ( \
    sp_da_value_t(__arr) __it, \
    *__sp_it(__it) = (__arr), \
    *__sp_it_end(__it) = __sp_it(__it) ? \
      __sp_it(__it) + sp_da_size(__sp_it(__it)) : \
      __sp_it(__it); \
  \
    __sp_it(__it) < __sp_it_end(__it) && \
    (__it = *__sp_it(__it), 1);\
  \
    __sp_it(__it)++ \
  )

#endif
