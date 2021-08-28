#ifndef __LOG_H__
#define __LOG_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* enable log output. default open this macro */
#define LOG_OUTPUT_ENABLE
/* enable assert check */
// #define LOG_ASSERT_ENABLE
/* enable log color */
#define LOG_COLOR_ENABLE
/* output filter's tag level max num */
#define LOG_FILTER_TAG_LVL_MAX_NUM          5

/* enable log write file.*/
#define LOG_FILE_ENABLE
/* log file name */
#define LOG_FILE_NAME      "/tmp/log_file.log"
/* EasyLogger file log plugin's using max rotate file count */
#define LOG_FILE_MAX_ROTATE 3
/* EasyLogger file log plugin's using file max size */
#define LOG_FILE_MAX_SIZE  (10 * 1024)

/* output log's level */
typedef enum {
    LOG_LVL_ASSERT = 0,
    LOG_LVL_ERROR,
    LOG_LVL_WARN,
    LOG_LVL_INFO,
    LOG_LVL_DEBUG,
    LOG_LVL_VERBOSE,
    LOG_LVL_MAX,
} LOG_LEVEL;

/* the output silent level and all level for filter setting */
#define LOG_FILTER_LVL_SILENT               LOG_LVL_ASSERT
#define LOG_FILTER_LVL_ALL                  LOG_LVL_VERBOSE

#define LOG_CHECK(condition, action)                \
  do {                                              \
    if (condition) {                                \
      log_a("check [" #condition "]\n");  \
      action;                                       \
    }                                               \
  } while (0)

int  log_init(void);
void log_deinit(void);
void log_set_output_enabled(bool enabled);
bool log_get_output_enabled(void);
void log_set_text_color_enabled(bool enabled);
bool log_get_text_color_enabled(void);
void log_set_fmt(uint8_t level, size_t set);
void log_set_filter(uint8_t level, const char *tag, const char *keyword);
void log_set_filter_lvl(uint8_t level);
void log_set_filter_tag(const char *tag);
void log_set_filter_kw(const char *keyword);
void log_set_filter_tag_lvl(const char *tag, uint8_t level);
int  log_get_filter_tag_lvl(const char *tag);
void log_raw(const char *format, ...);
void log_output_lock_enabled(bool enabled);
void log_assert_set_hook(void (*hook)(const char* expr, const char* func, size_t line));
int  log_find_lvl(const char *log);
const char *log_find_tag(const char *log, uint8_t lvl, size_t *tag_len);
void log_hexdump(const char *name, uint8_t width, uint8_t *buf, uint16_t size);

/* EasyLogger assert for developer. */
#ifdef LOG_ASSERT_ENABLE
    #define LOG_ASSERT(EXPR)                                                 \
    if (!(EXPR))                                                              \
    {                                                                         \
        if (log_assert_hook == NULL) {                                       \
            log_assert("log", "(%s) has assert failed at %s:%ld.", #EXPR, __FUNCTION__, __LINE__); \
            while (1);                                                        \
        } else {                                                              \
            log_assert_hook(#EXPR, __FUNCTION__, __LINE__);                  \
        }                                                                     \
    }
#else
    #define LOG_ASSERT(EXPR)                    ((void)0);
#endif

#ifndef LOG_OUTPUT_ENABLE
    #define log_assert(tag, ...)
    #define log_error(tag, ...)
    #define log_warn(tag, ...)
    #define log_info(tag, ...)
    #define log_debug(tag, ...)
    #define log_verbose(tag, ...)
#else /* LOG_OUTPUT_ENABLE */
    #define log_assert(tag, ...) \
            log_output(LOG_LVL_ASSERT, tag, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
    #define log_error(tag, ...) \
            log_output(LOG_LVL_ERROR, tag, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
    #define log_warn(tag, ...) \
            log_output(LOG_LVL_WARN, tag, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
    #define log_info(tag, ...) \
            log_output(LOG_LVL_INFO, tag, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
    #define log_debug(tag, ...) \
            log_output(LOG_LVL_DEBUG, tag, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
    #define log_verbose(tag, ...) \
            log_output(LOG_LVL_VERBOSE, tag, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#endif /* LOG_OUTPUT_ENABLE */

/* all formats index */
typedef enum {
    LOG_FMT_LVL    = 1 << 0, /**< level */
    LOG_FMT_TAG    = 1 << 1, /**< tag */
    LOG_FMT_TIME   = 1 << 2, /**< current time */
    LOG_FMT_P_INFO = 1 << 3, /**< process info */
    LOG_FMT_T_INFO = 1 << 4, /**< thread info */
    LOG_FMT_DIR    = 1 << 5, /**< file directory and name */
    LOG_FMT_FUNC   = 1 << 6, /**< function name */
    LOG_FMT_LINE   = 1 << 7, /**< line number */
} LOG_FMT;

/* macro definition for all formats */
#define LOG_FMT_ALL    (LOG_FMT_LVL|LOG_FMT_TAG|LOG_FMT_TIME|LOG_FMT_P_INFO|LOG_FMT_T_INFO| \
    LOG_FMT_DIR|LOG_FMT_FUNC|LOG_FMT_LINE)

extern void (*log_assert_hook)(const char* expr, const char* func, size_t line);
extern void log_output(uint8_t level, const char *tag, const char *file, const char *func,
        const long line, const char *format, ...);
/**
 * log API short definition
 * NOTE: The `LOG_TAG` and `LOG_LVL` must defined before including the <g_log.h> when you want to use log_x API.
 */
#if !defined(LOG_TAG)
    #define LOG_TAG          "NO_TAG"
#endif

#if !defined(LOG_LVL)
    #define LOG_LVL          LOG_LVL_VERBOSE
#endif
#if LOG_LVL >= LOG_LVL_ASSERT
    #define log_a(...)       log_assert(LOG_TAG, __VA_ARGS__)
#else
    #define log_a(...)       ((void)0);
#endif
#if LOG_LVL >= LOG_LVL_ERROR
    #define log_e(...)       log_error(LOG_TAG, __VA_ARGS__)
#else
    #define log_e(...)       ((void)0);
#endif
#if LOG_LVL >= LOG_LVL_WARN
    #define log_w(...)       log_warn(LOG_TAG, __VA_ARGS__)
#else
    #define log_w(...)       ((void)0);
#endif
#if LOG_LVL >= LOG_LVL_INFO
    #define log_i(...)       log_info(LOG_TAG, __VA_ARGS__)
#else
    #define log_i(...)       ((void)0);
#endif
#if LOG_LVL >= LOG_LVL_DEBUG
    #define log_d(...)       log_debug(LOG_TAG, __VA_ARGS__)
#else
    #define log_d(...)       ((void)0);
#endif
#if LOG_LVL >= LOG_LVL_VERBOSE
    #define log_v(...)       log_verbose(LOG_TAG, __VA_ARGS__)
#else
    #define log_v(...)       ((void)0);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LOG_H__ */
