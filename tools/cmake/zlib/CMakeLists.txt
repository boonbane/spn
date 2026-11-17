cmake_minimum_required(VERSION 3.24)

set(ZLIB_ROOT ${SOURCE_ROOT}/zlib)

if(NOT EXISTS ${ZLIB_ROOT}/zlib.h)
  message(FATAL_ERROR "zlib: ${ZLIB_ROOT} missing — run `make fetch`")
endif()

add_library(z STATIC
  ${ZLIB_ROOT}/adler32.c
  ${ZLIB_ROOT}/crc32.c
  ${ZLIB_ROOT}/deflate.c
  ${ZLIB_ROOT}/inffast.c
  ${ZLIB_ROOT}/inflate.c
  ${ZLIB_ROOT}/inftrees.c
  ${ZLIB_ROOT}/trees.c
  ${ZLIB_ROOT}/zutil.c
)
target_include_directories(z PUBLIC ${ZLIB_ROOT})
set_target_properties(z PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${STORE}/lib)
if(NOT MSVC)
  target_compile_options(z PRIVATE -O2)
endif()

add_library(minizip STATIC
  ${ZLIB_ROOT}/contrib/minizip/ioapi.c
  ${ZLIB_ROOT}/contrib/minizip/unzip.c
)
target_include_directories(minizip PUBLIC ${ZLIB_ROOT}/contrib)
target_link_libraries(minizip PUBLIC z)
target_compile_definitions(minizip PRIVATE NOUNCRYPT)
set_target_properties(minizip PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${STORE}/lib)
if(NOT MSVC)
  target_compile_options(minizip PRIVATE -O2)
  target_compile_definitions(minizip PRIVATE _GNU_SOURCE)
endif()
