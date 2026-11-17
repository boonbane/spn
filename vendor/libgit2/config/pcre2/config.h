#ifndef SPN_PCRE2_CONFIG_H
#define SPN_PCRE2_CONFIG_H

#define HAVE_ASSERT_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1

#if defined(_WIN32)
# define HAVE_WINDOWS_H 1
#else
# define HAVE_UNISTD_H 1
#endif

#if !defined(_MSC_VER)
# define HAVE_DIRENT_H 1
#endif

#if defined(__has_builtin)
# if __has_builtin(__builtin_mul_overflow)
#  define HAVE_BUILTIN_MUL_OVERFLOW 1
# endif
# if __has_builtin(__builtin_unreachable)
#  define HAVE_BUILTIN_UNREACHABLE 1
# endif
#elif defined(__GNUC__) && __GNUC__ >= 5
# define HAVE_BUILTIN_MUL_OVERFLOW 1
# define HAVE_BUILTIN_UNREACHABLE 1
#endif

#if defined(_MSC_VER)
# define HAVE_BUILTIN_ASSUME 1
#endif

#if defined(__has_attribute)
# if __has_attribute(uninitialized)
#  define HAVE_ATTRIBUTE_UNINITIALIZED 1
# endif
#endif

#define SUPPORT_PCRE2_8 1
#define SUPPORT_UNICODE 1

#define LINK_SIZE 2
#define HEAP_LIMIT 20000000
#define MATCH_LIMIT 10000000
#define MATCH_LIMIT_DEPTH MATCH_LIMIT
#define MAX_VARLOOKBEHIND 255
#define NEWLINE_DEFAULT 2
#define PARENS_NEST_LIMIT 250

#define MAX_NAME_SIZE 128
#define MAX_NAME_COUNT 10000

#endif
