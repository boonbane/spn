cmake_minimum_required(VERSION 3.24)

set(XZ_ROOT ${SOURCE_ROOT}/xz)
set(LZMA_ROOT ${XZ_ROOT}/src/liblzma)

if(NOT EXISTS ${LZMA_ROOT}/api/lzma.h)
  message(FATAL_ERROR "xz: ${XZ_ROOT} missing — run `make fetch`")
endif()

file(GLOB LZMA_SOURCES
  ${XZ_ROOT}/src/common/tuklib_physmem.c
  ${XZ_ROOT}/src/common/tuklib_cpucores.c
  ${LZMA_ROOT}/check/check.c
  ${LZMA_ROOT}/check/crc32_fast.c
  ${LZMA_ROOT}/check/crc64_fast.c
  ${LZMA_ROOT}/check/sha256.c
  ${LZMA_ROOT}/common/block_util.c
  ${LZMA_ROOT}/common/common.c
  ${LZMA_ROOT}/common/easy_preset.c
  ${LZMA_ROOT}/common/filter_common.c
  ${LZMA_ROOT}/common/hardware_physmem.c
  ${LZMA_ROOT}/common/hardware_cputhreads.c
  ${LZMA_ROOT}/common/index.c
  ${LZMA_ROOT}/common/outqueue.c
  ${LZMA_ROOT}/common/stream_flags_common.c
  ${LZMA_ROOT}/common/string_conversion.c
  ${LZMA_ROOT}/common/vli_size.c
  ${LZMA_ROOT}/common/*_decoder.c
  ${LZMA_ROOT}/common/easy_decoder_memusage.c
  ${LZMA_ROOT}/common/file_info.c
  ${LZMA_ROOT}/common/index_hash.c
  ${LZMA_ROOT}/common/stream_decoder_mt.c
  ${LZMA_ROOT}/delta/delta_common.c
  ${LZMA_ROOT}/delta/delta_decoder.c
  ${LZMA_ROOT}/lz/lz_decoder.c
  ${LZMA_ROOT}/lzma/lzma_decoder.c
  ${LZMA_ROOT}/lzma/lzma2_decoder.c
  ${LZMA_ROOT}/lzma/lzma_encoder_presets.c
  ${LZMA_ROOT}/simple/simple_coder.c
  ${LZMA_ROOT}/simple/simple_decoder.c
  ${LZMA_ROOT}/simple/x86.c
  ${LZMA_ROOT}/simple/arm.c
  ${LZMA_ROOT}/simple/armthumb.c
  ${LZMA_ROOT}/simple/arm64.c
  ${LZMA_ROOT}/simple/powerpc.c
  ${LZMA_ROOT}/simple/ia64.c
  ${LZMA_ROOT}/simple/sparc.c
  ${LZMA_ROOT}/simple/riscv.c
)

add_library(lzma STATIC ${LZMA_SOURCES})
target_include_directories(lzma PUBLIC ${LZMA_ROOT}/api)
target_include_directories(lzma PRIVATE
  ${XZ_ROOT}/src/common
  ${LZMA_ROOT}/common
  ${LZMA_ROOT}/check
  ${LZMA_ROOT}/lz
  ${LZMA_ROOT}/rangecoder
  ${LZMA_ROOT}/lzma
  ${LZMA_ROOT}/delta
  ${LZMA_ROOT}/simple
)
target_compile_definitions(lzma PUBLIC LZMA_API_STATIC)
target_compile_definitions(lzma PRIVATE
  HAVE_STDBOOL_H
  HAVE_INTTYPES_H
  HAVE_CHECK_CRC32
  HAVE_CHECK_CRC64
  HAVE_CHECK_SHA256
  HAVE_DECODERS
  HAVE_DECODER_LZMA1
  HAVE_DECODER_LZMA2
  HAVE_DECODER_DELTA
  HAVE_DECODER_X86
  HAVE_DECODER_ARM
  HAVE_DECODER_ARMTHUMB
  HAVE_DECODER_ARM64
  HAVE_DECODER_POWERPC
  HAVE_DECODER_IA64
  HAVE_DECODER_SPARC
  HAVE_DECODER_RISCV
  HAVE_LZIP_DECODER
  TUKLIB_SYMBOL_PREFIX=lzma_
)
set_target_properties(lzma PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${STORE}/lib)
if(NOT MSVC)
  target_compile_options(lzma PRIVATE -O2)
  target_compile_definitions(lzma PRIVATE _GNU_SOURCE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_compile_definitions(lzma PRIVATE MYTHREAD_VISTA)
else()
  target_compile_definitions(lzma PRIVATE MYTHREAD_POSIX HAVE_CLOCK_GETTIME TUKLIB_PHYSMEM_SYSCONF)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(lzma PRIVATE HAVE_CLOCK_MONOTONIC HAVE_PTHREAD_CONDATTR_SETCLOCK TUKLIB_CPUCORES_SCHED_GETAFFINITY)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(lzma PRIVATE TUKLIB_CPUCORES_SYSCTL HAVE_SYS_PARAM_H)
  endif()
endif()
