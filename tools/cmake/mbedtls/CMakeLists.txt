cmake_minimum_required(VERSION 3.24)

set(MBEDTLS_ROOT ${SOURCE_ROOT}/mbedtls)
set(MBEDTLS_CONFIG ${CMAKE_SOURCE_DIR}/vendor/mbedtls/config)

if(NOT EXISTS ${MBEDTLS_ROOT}/include/mbedtls/ssl.h)
  message(FATAL_ERROR "mbedtls: ${MBEDTLS_ROOT} missing — run `make fetch`")
endif()

file(GLOB MBEDTLS_SOURCES ${MBEDTLS_ROOT}/library/*.c)
add_library(mbedtls STATIC ${MBEDTLS_SOURCES})
target_include_directories(mbedtls PUBLIC ${MBEDTLS_ROOT}/include)
target_include_directories(mbedtls PRIVATE ${MBEDTLS_ROOT}/library ${MBEDTLS_CONFIG})
target_compile_definitions(mbedtls PUBLIC MBEDTLS_THREADING_PTHREAD MBEDTLS_THREADING_C)
target_compile_definitions(mbedtls PRIVATE MBEDTLS_USER_CONFIG_FILE="spn_mbedtls_user_config.h")
target_compile_options(mbedtls PRIVATE -O2)
set_target_properties(mbedtls PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${STORE}/lib)
