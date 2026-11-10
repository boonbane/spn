#ifndef SPN_CORE_H
#define SPN_CORE_H

#include "spn/guest.h"

typedef enum {
  SPN_ABI_NONE,
  SPN_ABI_GNU,
  SPN_ABI_MUSL,
  SPN_ABI_MSVC,
  SPN_ABI_APPLE,
  SPN_ABI_BARE,
  SPN_ABI_ELF,
  SPN_ABI_COUNT,
} spn_abi_t;

typedef enum {
  SPN_ARCH_NONE,
  SPN_ARCH_X64,
  SPN_ARCH_ARM64,
  SPN_ARCH_WASM32,
} spn_arch_t;

typedef struct {
  spn_arch_t arch;
  spn_os_t os;
  spn_abi_t abi;
} spn_triple_t;

typedef enum {
  SPN_CC_DRIVER_NONE,
  SPN_CC_DRIVER_GCC,
  SPN_CC_DRIVER_CLANG,
  SPN_CC_DRIVER_MSVC,
  SPN_CC_DRIVER_ZIG,
} spn_cc_driver_t;

typedef enum {
  SPN_LD_FAMILY_NONE,
  SPN_LD_FAMILY_GNU,
  SPN_LD_FAMILY_LLD,
  SPN_LD_FAMILY_LD64,
  SPN_LD_FAMILY_MSVC,
} spn_ld_family_t;

typedef enum {
  SPN_TOOLCHAIN_DETECT_NONE,
  SPN_TOOLCHAIN_DETECT_MSVC,
} spn_toolchain_detect_t;

typedef enum {
  SPN_OBJ_ELF,
  SPN_OBJ_COFF,
  SPN_OBJ_MACHO,
  SPN_OBJ_WASM,
} spn_obj_format_t;

typedef struct {
  u16 major;
  u16 minor;
} spn_os_version_t;

typedef struct {
  u32 major;
  u32 minor;
  u32 patch;
  u8 padding [4];
} spn_semver_t;

typedef enum {
  SPN_CC_FEATURE_LINK_EXE,
  SPN_CC_FEATURE_LINK_SHARED,
  SPN_CC_FEATURE_LINK_REACTOR,
  SPN_CC_FEATURE_ARCHIVE,
  SPN_CC_FEATURE_FRAMEWORKS,
  SPN_CC_FEATURE_LINKER_SCRIPT,
} spn_cc_feature_t;

typedef enum {
  SPN_OPTION_SETTER_NONE,
  SPN_OPTION_SETTER_DEFAULT,
  SPN_OPTION_SETTER_PROFILE,
  SPN_OPTION_SETTER_ROOT_MANIFEST,
  SPN_OPTION_SETTER_UNION,
  SPN_OPTION_SETTER_CONSUMER,
} spn_option_setter_kind_t;

typedef enum {
  SPN_LINKAGE_REQUESTER_PROFILE,
  SPN_LINKAGE_REQUESTER_ROOT_MANIFEST,
  SPN_LINKAGE_REQUESTER_LIBC,
  SPN_LINKAGE_REQUESTER_TARGET,
} spn_linkage_requester_t;

typedef enum {
  SPN_LIB_KIND_NONE,
  SPN_LIB_KIND_SHARED,
  SPN_LIB_KIND_STATIC,
  SPN_LIB_KIND_SOURCE,
  SPN_LIB_KIND_OBJECT,
} spn_linkage_t;

typedef enum {
  SPN_RUNTIME_NONE,
  SPN_RUNTIME_STATIC,
  SPN_RUNTIME_SHARED,
} spn_runtime_t;

typedef struct {
  spn_linkage_t linkage;
  spn_runtime_t runtime;
  spn_runtime_t libc;
} spn_linking_t;

typedef enum {
  SPN_LINKING_REFUSAL_NONE,
  SPN_LINKING_REFUSAL_NO_LOADER,
  SPN_LINKING_REFUSAL_OS_LIBC,
  SPN_LINKING_REFUSAL_OS_RUNTIME,
  SPN_LINKING_REFUSAL_SHARED_RUNTIME,
  SPN_LINKING_REFUSAL_HYBRID_CRT,
  SPN_LINKING_REFUSAL_SHARED_DEPS,
} spn_linking_refusal_t;

typedef enum {
  SPN_TARGET_KIND_LIB,
  SPN_TARGET_KIND_EXE,
  SPN_TARGET_KIND_SCRIPT,
  SPN_TARGET_KIND_TEST,
  SPN_TARGET_KIND_EXAMPLE,
  SPN_TARGET_KIND_CONFIGURE_METAPROGRAM,
  SPN_TARGET_KIND_BUILD_METAPROGRAM,
} spn_target_kind_t;

#define spn_target_kind_bit(kind) (1u << (kind))

typedef enum {
  SPN_WIN_SUBSYSTEM_NONE,
  SPN_WIN_SUBSYSTEM_CONSOLE,
  SPN_WIN_SUBSYSTEM_WINDOWS,
} spn_win_subsystem_t;

typedef enum {
  SPN_TREE_NONE,
  SPN_TREE_SOURCE,
  SPN_TREE_MANIFEST,
  SPN_TREE_WORK,
} spn_tree_t;

typedef enum {
  SPN_DIR_NONE = 0,
  SPN_DIR_CACHE = 1,
  SPN_DIR_STORE = 2,
  SPN_DIR_INCLUDE = 3,
  SPN_DIR_VENDOR = 4,
  SPN_DIR_LIB = 5,
  SPN_DIR_SOURCE = 6,
  SPN_DIR_WORK = 7,
  SPN_DIR_PROJECT = 8,
  SPN_DIR_MANIFEST = 9,
  SPN_DIR_BIN = 10,
} spn_dir_t;

typedef enum {
  SPN_C_STANDARD_NONE,
  SPN_C11,
  SPN_C99,
  SPN_C89,
  SPN_GNU11,
  SPN_GNU99,
  SPN_GNU89,
} spn_c_standard_t;

typedef enum {
  SPN_CXX_STANDARD_NONE,
  SPN_CXX11,
  SPN_CXX14,
  SPN_CXX17,
  SPN_CXX20,
  SPN_CXX23,
} spn_cxx_standard_t;

typedef enum {
  SPN_LANG_C,
  SPN_LANG_CXX,
  SPN_LANG_ASM,
} spn_lang_t;

typedef enum {
  SPN_CC_NONE,
  SPN_CC_GCC,
  SPN_CC_CLANG,
  SPN_CC_MUSL_GCC,
  SPN_CC_TCC,
  SPN_CC_COSMOCC,
  SPN_CC_ZIG,
  SPN_CC_CUSTOM,
} spn_cc_kind_t;

typedef enum {
  SPN_DEP_KIND_PACKAGE,
  SPN_DEP_KIND_BUILD,
  SPN_DEP_KIND_TEST,
} spn_dep_kind_t;

#endif
