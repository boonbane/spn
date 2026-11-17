cmake_minimum_required(VERSION 3.24)

set(CURL_ROOT ${SOURCE_ROOT}/curl)
set(CURL_CONFIG ${CMAKE_SOURCE_DIR}/vendor/curl/config)

if(NOT EXISTS ${CURL_ROOT}/include/curl/curl.h)
  message(FATAL_ERROR "curl: ${CURL_ROOT} missing — run `make fetch`")
endif()

file(GLOB CURL_SOURCES
  ${CURL_ROOT}/lib/*.c
  ${CURL_ROOT}/lib/curlx/*.c
  ${CURL_ROOT}/lib/vauth/*.c
  ${CURL_ROOT}/lib/vtls/*.c
  ${CURL_ROOT}/lib/vquic/*.c
  ${CURL_ROOT}/lib/vssh/*.c
)

add_library(curl STATIC ${CURL_SOURCES})
target_include_directories(curl PUBLIC ${CURL_ROOT}/include)
target_include_directories(curl PRIVATE ${CURL_ROOT}/lib ${CURL_CONFIG})
target_compile_definitions(curl PUBLIC CURL_STATICLIB)
target_compile_definitions(curl PRIVATE BUILDING_LIBCURL HAVE_CONFIG_H)
set_target_properties(curl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${STORE}/lib)
if(NOT MSVC)
  target_compile_options(curl PRIVATE -O2)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_link_libraries(curl INTERFACE ws2_32 iphlpapi bcrypt advapi32 crypt32 secur32)
else()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(curl PRIVATE _GNU_SOURCE)
  endif()
  target_link_libraries(curl PUBLIC mbedtls)
endif()
