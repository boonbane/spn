#ifndef INCLUDE_features_h__
#define INCLUDE_features_h__

#if defined(_WIN64) || defined(__LP64__)
# define GIT_ARCH_64 1
#else
# define GIT_ARCH_32 1
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
# define GIT_QSORT_MSC 1
#elif defined(__APPLE__)
# define GIT_QSORT_BSD 1
#elif defined(__linux__)
# define GIT_QSORT_GNU 1
#endif

#if defined(_WIN32)
# define GIT_IO_WSAPOLL 1
#else
# define GIT_FUTIMENS 1
# define GIT_IO_POLL 1
# define GIT_IO_SELECT 1
#endif

#if defined(__linux__)
# define GIT_RAND_GETENTROPY 1
#endif

#if defined(__linux__) || defined(__APPLE__)
# define GIT_RAND_GETLOADAVG 1
#endif

#if defined(__x86_64__) || defined(_M_X64)
# define GIT_BUILD_CPU "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
# define GIT_BUILD_CPU "aarch64"
#endif

#endif
