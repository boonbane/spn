#if defined(MBEDTLS_THREADING_C)
  #undef MBEDTLS_SELF_TEST
#endif

#define MBEDTLS_PLATFORM_DEV_RANDOM "/dev/urandom"
