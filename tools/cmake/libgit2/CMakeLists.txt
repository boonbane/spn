cmake_minimum_required(VERSION 3.24)

set(GIT2_ROOT ${SOURCE_ROOT}/libgit2)
set(GIT2_CONFIG ${CMAKE_SOURCE_DIR}/vendor/libgit2/config)
set(MBEDTLS_ROOT ${SOURCE_ROOT}/mbedtls)
set(MBEDTLS_CONFIG ${CMAKE_SOURCE_DIR}/vendor/mbedtls/config)

if(NOT EXISTS ${GIT2_ROOT}/include/git2.h)
  message(FATAL_ERROR "libgit2: ${GIT2_ROOT} missing — run `make fetch`")
endif()

set(GIT2_PLATFORM_DEFINES "")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND GIT2_PLATFORM_DEFINES WIN32)
  if(MSVC)
    list(APPEND GIT2_PLATFORM_DEFINES _SCL_SECURE_NO_WARNINGS _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
  else()
    list(APPEND GIT2_PLATFORM_DEFINES _GNU_SOURCE __USE_MINGW_ANSI_STDIO=1)
  endif()
else()
  list(APPEND GIT2_PLATFORM_DEFINES _FILE_OFFSET_BITS=64 _GNU_SOURCE)
endif()

set(GIT2_OPTIMIZE "")
if(NOT MSVC)
  set(GIT2_OPTIMIZE -O2)
endif()

file(GLOB GIT2_PCRE2_SOURCES
  ${GIT2_ROOT}/deps/pcre2/pcre2_auto_possess.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_chartables.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_chkdint.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_compile.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_compile_cgroup.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_compile_class.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_config.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_context.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_convert.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_dfa_match.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_error.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_extuni.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_find_bracket.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_maketables.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_match.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_match_data.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_match_next.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_newline.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_ord2utf.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_pattern_info.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_script_run.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_serialize.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_string_utils.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_study.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_substitute.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_substring.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_tables.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_ucd.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_valid_utf.c
  ${GIT2_ROOT}/deps/pcre2/pcre2_xclass.c
)
add_library(git2_pcre2 OBJECT ${GIT2_PCRE2_SOURCES})
target_include_directories(git2_pcre2 PRIVATE ${GIT2_ROOT}/deps/pcre2 ${GIT2_CONFIG}/pcre2)
target_compile_definitions(git2_pcre2 PRIVATE
  ${GIT2_PLATFORM_DEFINES}
  HAVE_CONFIG_H
  PCRE2_CODE_UNIT_WIDTH=8
  PCRE2_STATIC
  PCRE2_EXPORT=
  PCRE2_EXP_DECL=
  PCRE2_EXP_DEFN=
)
target_compile_options(git2_pcre2 PRIVATE ${GIT2_OPTIMIZE})
if(MSVC)
  target_compile_definitions(git2_pcre2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

file(GLOB GIT2_ZLIB_SOURCES ${GIT2_ROOT}/deps/zlib/*.c)
add_library(git2_zlib OBJECT ${GIT2_ZLIB_SOURCES})
target_include_directories(git2_zlib PRIVATE ${GIT2_ROOT}/deps/zlib)
target_compile_definitions(git2_zlib PRIVATE
  ${GIT2_PLATFORM_DEFINES}
  NO_VIZ
  STDC
  NO_GZIP
  HAVE_SYS_TYPES_H
  HAVE_STDINT_H
  HAVE_STDDEF_H
)
target_compile_options(git2_zlib PRIVATE ${GIT2_OPTIMIZE})
if(MINGW)
  target_compile_definitions(git2_zlib PRIVATE Z_HAVE_UNISTD_H _LFS64_LARGEFILE _LARGEFILE64_SOURCE=1)
endif()

file(GLOB GIT2_SOURCES
  ${GIT2_ROOT}/src/libgit2/*.c
  ${GIT2_ROOT}/src/libgit2/streams/*.c
  ${GIT2_ROOT}/src/libgit2/transports/*.c
  ${GIT2_ROOT}/src/util/*.c
  ${GIT2_ROOT}/src/util/allocators/*.c
  ${GIT2_ROOT}/src/util/hash/collisiondetect.c
  ${GIT2_ROOT}/src/util/hash/sha1dc/*.c
  ${GIT2_ROOT}/src/util/hash/builtin.c
  ${GIT2_ROOT}/src/util/hash/rfc6234/*.c
  ${GIT2_ROOT}/deps/reftable/*.c
  ${GIT2_ROOT}/deps/xdiff/*.c
  ${GIT2_ROOT}/deps/llhttp/*.c
)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  file(GLOB GIT2_OS_SOURCES ${GIT2_ROOT}/src/util/win32/*.c)
else()
  file(GLOB GIT2_OS_SOURCES ${GIT2_ROOT}/src/util/unix/*.c)
endif()

add_library(git2 STATIC ${GIT2_SOURCES} ${GIT2_OS_SOURCES} $<TARGET_OBJECTS:git2_pcre2> $<TARGET_OBJECTS:git2_zlib>)
target_include_directories(git2 PUBLIC ${GIT2_ROOT}/include)
target_include_directories(git2 PRIVATE
  ${GIT2_ROOT}/src/libgit2
  ${GIT2_ROOT}/src/util
  ${GIT2_ROOT}/deps/reftable
  ${GIT2_ROOT}/deps/xdiff
  ${GIT2_ROOT}/deps/llhttp
  ${GIT2_ROOT}/deps/pcre2
  ${GIT2_ROOT}/deps/zlib
  ${GIT2_CONFIG}
)
target_compile_definitions(git2 PUBLIC GIT_STATIC)
target_compile_definitions(git2 PRIVATE
  ${GIT2_PLATFORM_DEFINES}
  GIT_THREADS
  GIT_HTTP
  GIT_HTTPS
  GIT_SHA1_BUILTIN
  SHA1DC_NO_STANDARD_INCLUDES=1
  SHA1DC_CUSTOM_INCLUDE_SHA1_C="git2_util.h"
  SHA1DC_CUSTOM_INCLUDE_UBC_CHECK_C="git2_util.h"
  SHA1DC_FORCE_ALIGNED_ACCESS
  GIT_SHA256_BUILTIN
  GIT_HTTPPARSER_BUILTIN
  GIT_REGEX_BUILTIN
  PCRE2_STATIC
  PCRE2_EXPORT=
  PCRE2_EXP_DECL=
  PCRE2_EXP_DEFN=
  GIT_COMPRESSION_BUILTIN
  GIT_SSH
  GIT_SSH_EXEC
  GIT_NSEC
)
target_compile_options(git2 PRIVATE ${GIT2_OPTIMIZE})
set_target_properties(git2 PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${STORE}/lib)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_compile_definitions(git2 PRIVATE GIT_THREADS_WIN32 GIT_HTTPS_WINHTTP GIT_NSEC_WIN32)
  target_link_libraries(git2 INTERFACE ws2_32 winhttp rpcrt4 crypt32 ole32)
  if(MINGW)
    target_include_directories(git2 PRIVATE ${GIT2_ROOT}/deps/winhttp)
  endif()
else()
  file(GLOB MBEDTLS_SOURCES ${MBEDTLS_ROOT}/library/*.c)
  add_library(mbedtls STATIC ${MBEDTLS_SOURCES})
  target_include_directories(mbedtls PUBLIC ${MBEDTLS_ROOT}/include)
  target_include_directories(mbedtls PRIVATE ${MBEDTLS_ROOT}/library ${MBEDTLS_CONFIG})
  target_compile_definitions(mbedtls PUBLIC MBEDTLS_THREADING_PTHREAD MBEDTLS_THREADING_C)
  target_compile_definitions(mbedtls PRIVATE MBEDTLS_USER_CONFIG_FILE="spn_mbedtls_user_config.h")
  target_compile_options(mbedtls PRIVATE ${GIT2_OPTIMIZE})
  set_target_properties(mbedtls PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${STORE}/lib)

  target_compile_definitions(git2 PRIVATE GIT_THREADS_PTHREADS GIT_HTTPS_MBEDTLS)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(git2 PRIVATE GIT_NSEC_MTIMESPEC)
  else()
    target_compile_definitions(git2 PRIVATE GIT_NSEC_MTIM)
  endif()
  target_link_libraries(git2 PUBLIC mbedtls)
endif()
