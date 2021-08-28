
#define LOG_TAG "log"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/time.h>

#include <log.h>

/* buffer size for every line's log */
#define LOG_LINE_BUF_SIZE                   1024
/* output line number max length */
#define LOG_LINE_NUM_MAX_LEN                5
/* output filter's tag max length */
#define LOG_FILTER_TAG_MAX_LEN              16
/* output filter's keyword max length */
#define LOG_FILTER_KW_MAX_LEN               16
/* output newline sign */
#define LOG_NEWLINE_SIGN                    "\n"

#ifdef LOG_COLOR_ENABLE

#define CSI_START "\033["
#define CSI_END "\033[0m"

/* output log front color */
#define F_BLACK "30;"
#define F_RED "31;"
#define F_GREEN "32;"
#define F_YELLOW "33;"
#define F_BLUE "34;"
#define F_MAGENTA "35;"
#define F_CYAN "36;"
#define F_WHITE "37;"
/* output log background color */
#define B_NULL
#define B_BLACK "40;"
#define B_RED "41;"
#define B_GREEN "42;"
#define B_YELLOW "43;"
#define B_BLUE "44;"
#define B_MAGENTA "45;"
#define B_CYAN "46;"
#define B_WHITE "47;"
/* output log fonts style */
#define S_BOLD "1m"
#define S_UNDERLINE "4m"
#define S_BLINK "5m"
#define S_NORMAL "22m"

/* output log default color definition: [front color] + [background color] + [show style] */
#define LOG_COLOR_ASSERT    (F_MAGENTA B_NULL S_NORMAL)
#define LOG_COLOR_ERROR     (F_RED B_NULL S_NORMAL)
#define LOG_COLOR_WARN      (F_YELLOW B_NULL S_NORMAL)
#define LOG_COLOR_INFO      (F_CYAN B_NULL S_NORMAL)
#define LOG_COLOR_DEBUG     (F_GREEN B_NULL S_NORMAL)
#define LOG_COLOR_VERBOSE   (F_BLUE B_NULL S_NORMAL)

#endif /* LOG_COLOR_ENABLE */

#ifdef linux
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

/* output log's tag filter */
typedef struct {
    uint8_t level;
    char tag[LOG_FILTER_TAG_MAX_LEN + 1];
    bool tag_use_flag; /**< false : tag is no used   true: tag is used */
} log_tag_lvl_filter_t;

/* output log's filter */
typedef struct {
    uint8_t level;
    char tag[LOG_FILTER_TAG_MAX_LEN + 1];
    char keyword[LOG_FILTER_KW_MAX_LEN + 1];
    log_tag_lvl_filter_t tag_lvl[LOG_FILTER_TAG_LVL_MAX_NUM];
} log_filter_t;

/* easy logger */
typedef struct {
    log_filter_t filter;
    size_t enabled_fmt_set[LOG_LVL_MAX];
    bool init_ok;
    bool output_enabled;
    bool output_lock_enabled;
    bool output_is_locked_before_enable;
    bool output_is_locked_before_disable;

#ifdef LOG_COLOR_ENABLE
    bool text_color_enabled;
#endif

#ifdef LOG_FILE_ENABLE
    char *name;              /* file name */
    FILE *fp;                /* file descriptor */
    size_t max_size;         /* file max size */
    int max_rotate;          /* max rotate file count */
#endif

} log_t;

/* log */
static log_t g_log = {0};
/* every line log's buffer */
static char log_buf[LOG_LINE_BUF_SIZE] = {0};
/* level output info */
static const char *level_output_info[] = {
    [LOG_LVL_ASSERT] = "A/",
    [LOG_LVL_ERROR] = "E/",
    [LOG_LVL_WARN] = "W/",
    [LOG_LVL_INFO] = "I/",
    [LOG_LVL_DEBUG] = "D/",
    [LOG_LVL_VERBOSE] = "V/",
};

#ifdef LOG_COLOR_ENABLE
/* color output info */
static const char *color_output_info[] = {
    [LOG_LVL_ASSERT] = LOG_COLOR_ASSERT,
    [LOG_LVL_ERROR] = LOG_COLOR_ERROR,
    [LOG_LVL_WARN] = LOG_COLOR_WARN,
    [LOG_LVL_INFO] = LOG_COLOR_INFO,
    [LOG_LVL_DEBUG] = LOG_COLOR_DEBUG,
    [LOG_LVL_VERBOSE] = LOG_COLOR_VERBOSE,
};
#endif /* LOG_COLOR_ENABLE */

void (*log_assert_hook)(const char *expr, const char *func, size_t line);

/* port */
static pthread_mutex_t output_lock;

#ifdef LOG_FILE_ENABLE

#define LOG_FILE_SEM_KEY   ((key_t)0x19910612)
#ifdef _SEM_SEMUN_UNDEFINED
union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux-specific) */
};
#endif

static int semid = -1;
static struct sembuf const up = {0, 1, SEM_UNDO};
static struct sembuf const down = {0, -1, SEM_UNDO};
#endif

/* log */
static bool get_fmt_enabled(uint8_t level, size_t set);

/* port */
static int log_port_init(void);
static int log_port_deinit(void);
static void log_port_output(const char *log, size_t size);
static void log_port_output_lock(void);
static void log_port_output_unlock(void);
static const char *log_port_get_time(void);
static const char *log_port_get_p_info(void);
static const char *log_port_get_t_info(void);

#ifdef LOG_FILE_ENABLE
static int log_file_port_init(void);
static void inline log_file_port_lock(void);
static void inline log_file_port_unlock(void);
static void log_file_port_deinit(void);
#endif

/**
 * another copy string function
 *
 * @param cur_len current copied log length, max size is LOG_LINE_BUF_SIZE
 * @param dst destination
 * @param src source
 *
 * @return copied length
 */
static size_t log_strcpy(size_t cur_len, char *dst, const char *src)
{
    const char *src_old = src;

    if(!dst || !src)
        return 0;

    while (*src != 0) {
        /* make sure destination has enough space */
        if (cur_len++ < LOG_LINE_BUF_SIZE) {
            *dst++ = *src++;
        } else {
            break;
        }
    }
    return src - src_old;
}

#ifdef LOG_FILE_ENABLE
static int log_file_init(void)
{
    log_file_port_init();

    g_log.name = LOG_FILE_NAME;
    g_log.max_size = LOG_FILE_MAX_SIZE;
    g_log.max_rotate = LOG_FILE_MAX_ROTATE;

    log_file_port_lock();

    if (g_log.name != NULL && strlen(g_log.name) > 0)
        g_log.fp = fopen(g_log.name, "a+");

    log_file_port_unlock();

    return 0;
}

/*
 * rotate the log file xxx.log.n-1 => xxx.log.n, and xxx.log => xxx.log.0
 */
static bool log_file_rotate(void)
{
#define SUFFIX_LEN 10
    /* mv xxx.log.n-1 => xxx.log.n, and xxx.log => xxx.log.0 */
    int n, err = 0;
    char oldpath[256], newpath[256];
    size_t base = strlen(g_log.name);
    bool result = true;
    FILE *tmp_fp;

    memcpy(oldpath, g_log.name, base);
    memcpy(newpath, g_log.name, base);

    fclose(g_log.fp);

    for (n = g_log.max_rotate - 1; n >= 0; --n) {
        snprintf(oldpath + base, SUFFIX_LEN, n ? ".%d" : "", n - 1);
        snprintf(newpath + base, SUFFIX_LEN, ".%d", n);
        /* remove the old file */
        if ((tmp_fp = fopen(newpath, "r")) != NULL) {
            fclose(tmp_fp);
            remove(newpath);
        }
        /* change the new log file to old file name */
        if ((tmp_fp = fopen(oldpath, "r")) != NULL) {
            fclose(tmp_fp);
            err = rename(oldpath, newpath);
        }

        if (err < 0) {
            result = false;
            goto __exit;
        }
    }

__exit:
    /* reopen the file */
    g_log.fp = fopen(g_log.name, "a+");

    return result;
}


static void log_file_write(const char *log, size_t size)
{
    size_t file_size = 0;

    LOG_CHECK(log == NULL, return;);

    log_file_port_lock();

    fseek(g_log.fp, 0L, SEEK_END);
    file_size = ftell(g_log.fp);

    if (unlikely(file_size > g_log.max_size)) {
#if LOG_FILE_MAX_ROTATE > 0
        if (!log_file_rotate()) {
            goto __exit;
        }
#else
        goto __exit;
#endif
    }

    fwrite(log, size, 1, g_log.fp);

    fflush(g_log.fp);

__exit:
    log_file_port_unlock();
}

static void log_file_deinit(void)
{
    log_file_port_lock();

    if (g_log.fp) {
        fclose(g_log.fp);
        g_log.fp = NULL;
    }

    log_file_port_unlock();

    log_file_port_deinit();

}
#endif

/**
 * EasyLogger initialize.
 *
 * @return result
 */
int log_init(void)
{
    int ret = 0;

    if (g_log.init_ok) {
        return ret;
    }

    /* port initialize */
    ret = log_port_init();
    if (ret != 0) {
        return ret;
    }

#ifdef LOG_FILE_ENABLE
    log_file_init();
#endif

    /* enable the output lock */
    log_output_lock_enabled(true);
    /* output locked status initialize */
    g_log.output_is_locked_before_enable = false;
    g_log.output_is_locked_before_disable = false;

#ifdef LOG_COLOR_ENABLE
    /* disable text color by default */
    log_set_text_color_enabled(true);
#endif

    /* set level is LOG_LVL_VERBOSE */
    log_set_filter_lvl(LOG_LVL_VERBOSE);

    /* set default log format */
    log_set_fmt(LOG_LVL_ASSERT, LOG_FMT_ALL & ~LOG_FMT_P_INFO & ~LOG_FMT_T_INFO);
    log_set_fmt(LOG_LVL_ERROR, LOG_FMT_LVL | LOG_FMT_TAG | LOG_FMT_TIME | LOG_FMT_DIR);
    log_set_fmt(LOG_LVL_WARN, LOG_FMT_LVL | LOG_FMT_TAG | LOG_FMT_TIME | LOG_FMT_DIR);
    log_set_fmt(LOG_LVL_INFO, LOG_FMT_LVL | LOG_FMT_TAG | LOG_FMT_TIME);
    log_set_fmt(LOG_LVL_DEBUG, LOG_FMT_ALL & ~LOG_FMT_P_INFO & ~LOG_FMT_T_INFO);
    log_set_fmt(LOG_LVL_VERBOSE, LOG_FMT_ALL);

    /* close printf buffer */
    setbuf(stdout, NULL);

    /* enable output */
    log_set_output_enabled(true);

    g_log.init_ok = true;

    return ret;
}

/**
 * EasyLogger deinitialize.
 *
 */
void log_deinit(void)
{
    if (!g_log.init_ok) {
        return;
    }

    /* port deinitialize */
    log_port_deinit();

#ifdef LOG_FILE_ENABLE
    log_file_deinit();
#endif

    g_log.init_ok = false;
}

/**
 * set output enable or disable
 *
 * @param enabled TRUE: enable FALSE: disable
 */
void log_set_output_enabled(bool enabled)
{
    g_log.output_enabled = enabled;
}

#ifdef LOG_COLOR_ENABLE
/**
 * set log text color enable or disable
 *
 * @param enabled TRUE: enable FALSE:disable
 */
void log_set_text_color_enabled(bool enabled)
{
    g_log.text_color_enabled = enabled;
}

/**
 * get log text color enable status
 *
 * @return enable or disable
 */
bool log_get_text_color_enabled(void)
{
    return g_log.text_color_enabled;
}
#endif /* LOG_COLOR_ENABLE */

/**
 * get output is enable or disable
 *
 * @return enable or disable
 */
bool log_get_output_enabled(void)
{
    return g_log.output_enabled;
}

/**
 * set log output format. only enable or disable
 *
 * @param level level
 * @param set format set
 */
void log_set_fmt(uint8_t level, size_t set)
{
    LOG_CHECK(level > LOG_LVL_VERBOSE, return;);

    g_log.enabled_fmt_set[level] = set;
}

/**
 * set log filter all parameter
 *
 * @param level level
 * @param tag tag
 * @param keyword keyword
 */
void log_set_filter(uint8_t level, const char *tag, const char *keyword)
{
    LOG_CHECK(level > LOG_LVL_VERBOSE, return;);

    log_set_filter_lvl(level);
    log_set_filter_tag(tag);
    log_set_filter_kw(keyword);
}

/**
 * set log filter's level
 *
 * @param level level
 */
void log_set_filter_lvl(uint8_t level)
{
    LOG_CHECK(level > LOG_LVL_VERBOSE, return;);

    g_log.filter.level = level;
}

/**
 * set log filter's tag
 *
 * @param tag tag
 */
void log_set_filter_tag(const char *tag)
{
    strncpy(g_log.filter.tag, tag, LOG_FILTER_TAG_MAX_LEN);
}

/**
 * set log filter's keyword
 *
 * @param keyword keyword
 */
void log_set_filter_kw(const char *keyword)
{
    strncpy(g_log.filter.keyword, keyword, LOG_FILTER_KW_MAX_LEN);
}

/**
 * lock output
 */
void log_output_lock(void)
{
    if (g_log.output_lock_enabled) {
        log_port_output_lock();
        g_log.output_is_locked_before_disable = true;
    } else {
        g_log.output_is_locked_before_enable = true;
    }
}

/**
 * unlock output
 */
void log_output_unlock(void)
{
    if (g_log.output_lock_enabled) {
        log_port_output_unlock();
        g_log.output_is_locked_before_disable = false;
    } else {
        g_log.output_is_locked_before_enable = false;
    }
}

/**
 * Set the filter's level by different tag.
 * The log on this tag which level is less than it will stop output.
 *
 * example:
 *     // the example tag log enter silent mode
 *     log_set_filter_tag_lvl("example", LOG_FILTER_LVL_SILENT);
 *     // the example tag log which level is less than INFO level will stop output
 *     log_set_filter_tag_lvl("example", LOG_LVL_INFO);
 *     // remove example tag's level filter, all level log will resume output
 *     log_set_filter_tag_lvl("example", LOG_FILTER_LVL_ALL);
 *
 * @param tag log tag
 * @param level The filter level. When the level is LOG_FILTER_LVL_SILENT, the log enter silent mode.
 *        When the level is LOG_FILTER_LVL_ALL, it will remove this tag's level filer.
 *        Then all level log will resume output.
 *
 */
void log_set_filter_tag_lvl(const char *tag, uint8_t level)
{
    LOG_CHECK(level > LOG_LVL_VERBOSE, return;);
    LOG_CHECK(tag == NULL, return;);
    uint8_t i = 0;

    if (!g_log.init_ok) {
        return;
    }

    log_port_output_lock();
    /* find the tag in arr */
    for (i = 0; i < LOG_FILTER_TAG_LVL_MAX_NUM; i++) {
        if (g_log.filter.tag_lvl[i].tag_use_flag == true
            && !strncmp(tag, g_log.filter.tag_lvl[i].tag, LOG_FILTER_TAG_MAX_LEN)) {
            break;
        }
    }

    if (i < LOG_FILTER_TAG_LVL_MAX_NUM) {
        /* find OK */
        if (level == LOG_FILTER_LVL_ALL) {
            /* remove current tag's level filter when input level is the lowest level */
            g_log.filter.tag_lvl[i].tag_use_flag = false;
            memset(g_log.filter.tag_lvl[i].tag, '\0', LOG_FILTER_TAG_MAX_LEN + 1);
            g_log.filter.tag_lvl[i].level = LOG_FILTER_LVL_SILENT;
        } else {
            g_log.filter.tag_lvl[i].level = level;
        }
    } else {
        /* only add the new tag's level filer when level is not LOG_FILTER_LVL_ALL */
        if (level != LOG_FILTER_LVL_ALL) {
            for (i = 0; i < LOG_FILTER_TAG_LVL_MAX_NUM; i++) {
                if (g_log.filter.tag_lvl[i].tag_use_flag == false) {
                    strncpy(g_log.filter.tag_lvl[i].tag, tag, LOG_FILTER_TAG_MAX_LEN);
                    g_log.filter.tag_lvl[i].level = level;
                    g_log.filter.tag_lvl[i].tag_use_flag = true;
                    break;
                }
            }
        }
    }
    log_output_unlock();
}

/**
 * get the level on tag's level filer
 *
 * @param tag tag
 *
 * @return It will return the lowest level when tag was not found.
 *         Other level will return when tag was found.
 */
int log_get_filter_tag_lvl(const char *tag)
{
    LOG_CHECK(tag == NULL, return -1;);
    uint8_t i = 0;
    uint8_t level = LOG_FILTER_LVL_ALL;

    if (!g_log.init_ok) {
        return level;
    }

    log_port_output_lock();
    /* find the tag in arr */
    for (i = 0; i < LOG_FILTER_TAG_LVL_MAX_NUM; i++) {
        if (g_log.filter.tag_lvl[i].tag_use_flag == true
            && !strncmp(tag, g_log.filter.tag_lvl[i].tag, LOG_FILTER_TAG_MAX_LEN)) {
            level = g_log.filter.tag_lvl[i].level;
            break;
        }
    }
    log_output_unlock();

    return level;
}

/**
 * output RAW format log
 *
 * @param format output format
 * @param ... args
 */
void log_raw(const char *format, ...)
{
    va_list args;
    size_t log_len = 0;
    int fmt_result;

    if (!g_log.init_ok) {
        return;
    }

    /* check output enabled */
    if (!g_log.output_enabled) {
        return;
    }

    /* args point to the first variable parameter */
    va_start(args, format);

    /* lock output */
    log_output_lock();

    /* package log data to buffer */
    fmt_result = vsnprintf(log_buf, LOG_LINE_BUF_SIZE, format, args);

    /* output converted log */
    if ((fmt_result > -1) && (fmt_result <= LOG_LINE_BUF_SIZE)) {
        log_len = fmt_result;
    } else {
        log_len = LOG_LINE_BUF_SIZE;
    }
    /* output log */
    log_port_output(log_buf, log_len);

#ifdef LOG_FILE_ENABLE
    /* write the file */
    log_file_write(log_buf, log_len);
#endif

    /* unlock output */
    log_output_unlock();

    va_end(args);
}

/**
 * output the log
 *
 * @param level level
 * @param tag tag
 * @param file file name
 * @param func function name
 * @param line line number
 * @param format output format
 * @param ... args
 *
 */
void log_output(uint8_t level, const char *tag, const char *file, const char *func, const long line,
                 const char *format, ...)
{
    size_t tag_len = strlen(tag), newline_len = strlen(LOG_NEWLINE_SIGN);
    int log_len = 0;
    char line_num[LOG_LINE_NUM_MAX_LEN + 1] = {0};
    char tag_sapce[LOG_FILTER_TAG_MAX_LEN / 2 + 1] = {0};
    va_list args;
    int fmt_result;

    LOG_CHECK(level > LOG_LVL_VERBOSE, return;);

    if (!g_log.init_ok) {
        return;
    }

    /* check output enabled */
    if (!g_log.output_enabled) {
        return;
    }
    /* level filter */
    if (level > g_log.filter.level || level > log_get_filter_tag_lvl(tag)) {
        return;
    } else if (!strstr(tag, g_log.filter.tag)) { /* tag filter */
        return;
    }
    /* args point to the first variable parameter */
    va_start(args, format);
    /* lock output */
    log_output_lock();

#ifdef LOG_COLOR_ENABLE
    /* add CSI start sign and color info */
    if (g_log.text_color_enabled) {
        log_len += log_strcpy(log_len, log_buf + log_len, CSI_START);
        log_len += log_strcpy(log_len, log_buf + log_len, color_output_info[level]);
    }
#endif

    /* package level info */
    if (get_fmt_enabled(level, LOG_FMT_LVL)) {
        log_len += log_strcpy(log_len, log_buf + log_len, level_output_info[level]);
    }
    /* package tag info */
    if (get_fmt_enabled(level, LOG_FMT_TAG)) {
        log_len += log_strcpy(log_len, log_buf + log_len, tag);
        /* if the tag length is less than 50% LOG_FILTER_TAG_MAX_LEN, then fill space */
        if (tag_len <= LOG_FILTER_TAG_MAX_LEN / 2) {
            memset(tag_sapce, ' ', LOG_FILTER_TAG_MAX_LEN / 2 - tag_len);
            log_len += log_strcpy(log_len, log_buf + log_len, tag_sapce);
        }
        log_len += log_strcpy(log_len, log_buf + log_len, " ");
    }
    /* package time, process and thread info */
    if (get_fmt_enabled(level, LOG_FMT_TIME | LOG_FMT_P_INFO | LOG_FMT_T_INFO)) {
        log_len += log_strcpy(log_len, log_buf + log_len, "[");
        /* package time info */
        if (get_fmt_enabled(level, LOG_FMT_TIME)) {
            log_len += log_strcpy(log_len, log_buf + log_len, log_port_get_time());
            if (get_fmt_enabled(level, LOG_FMT_P_INFO | LOG_FMT_T_INFO)) {
                log_len += log_strcpy(log_len, log_buf + log_len, " ");
            }
        }
        /* package process info */
        if (get_fmt_enabled(level, LOG_FMT_P_INFO)) {
            log_len += log_strcpy(log_len, log_buf + log_len, log_port_get_p_info());
            if (get_fmt_enabled(level, LOG_FMT_T_INFO)) {
                log_len += log_strcpy(log_len, log_buf + log_len, " ");
            }
        }
        /* package thread info */
        if (get_fmt_enabled(level, LOG_FMT_T_INFO)) {
            log_len += log_strcpy(log_len, log_buf + log_len, log_port_get_t_info());
        }
        log_len += log_strcpy(log_len, log_buf + log_len, "] ");
    }
    /* package file directory and name, function name and line number info */
    if (get_fmt_enabled(level, LOG_FMT_DIR | LOG_FMT_FUNC | LOG_FMT_LINE)) {
        log_len += log_strcpy(log_len, log_buf + log_len, "(");
        /* package file info */
        if (get_fmt_enabled(level, LOG_FMT_DIR)) {
            log_len += log_strcpy(log_len, log_buf + log_len, file);
            if (get_fmt_enabled(level, LOG_FMT_FUNC)) {
                log_len += log_strcpy(log_len, log_buf + log_len, ":");
            } else if (get_fmt_enabled(level, LOG_FMT_LINE)) {
                log_len += log_strcpy(log_len, log_buf + log_len, " ");
            }
        }
        /* package line info */
        if (get_fmt_enabled(level, LOG_FMT_LINE)) {
            snprintf(line_num, LOG_LINE_NUM_MAX_LEN, "%ld", line);
            log_len += log_strcpy(log_len, log_buf + log_len, line_num);
            if (get_fmt_enabled(level, LOG_FMT_FUNC)) {
                log_len += log_strcpy(log_len, log_buf + log_len, " ");
            }
        }
        /* package func info */
        if (get_fmt_enabled(level, LOG_FMT_FUNC)) {
            log_len += log_strcpy(log_len, log_buf + log_len, func);
        }
        log_len += log_strcpy(log_len, log_buf + log_len, ")");
    }
    /* package other log data to buffer. '\0' must be added in the end by vsnprintf. */
    fmt_result = vsnprintf(log_buf + log_len, LOG_LINE_BUF_SIZE - log_len, format, args);

    va_end(args);
    /* calculate log length */
    if ((log_len + fmt_result <= LOG_LINE_BUF_SIZE) && (fmt_result > -1)) {
        log_len += fmt_result;
    } else {
        /* using max length */
        log_len = LOG_LINE_BUF_SIZE;
    }
    /* overflow check and reserve some space for CSI end sign and newline sign */
#ifdef LOG_COLOR_ENABLE
    if (log_len + (sizeof(CSI_END) - 1) + newline_len > LOG_LINE_BUF_SIZE) {
        /* using max length */
        log_len = LOG_LINE_BUF_SIZE;
        /* reserve some space for CSI end sign */
        log_len -= (sizeof(CSI_END) - 1);
#else
    if (log_len + newline_len > LOG_LINE_BUF_SIZE) {
        /* using max length */
        log_len = LOG_LINE_BUF_SIZE;
#endif /* LOG_COLOR_ENABLE */
        /* reserve some space for newline sign */
        log_len -= newline_len;
    }
    /* keyword filter */
    if (g_log.filter.keyword[0] != '\0') {
        /* add string end sign */
        log_buf[log_len] = '\0';
        /* find the keyword */
        if (!strstr(log_buf, g_log.filter.keyword)) {
            /* unlock output */
            log_output_unlock();
            return;
        }
    }

#ifdef LOG_COLOR_ENABLE
    /* add CSI end sign */
    if (g_log.text_color_enabled) {
        log_len += log_strcpy(log_len, log_buf + log_len, CSI_END);
    }
#endif

    /* package newline sign */
    log_len += log_strcpy(log_len, log_buf + log_len, LOG_NEWLINE_SIGN);
    /* output log */
    log_port_output(log_buf, log_len);

#ifdef LOG_FILE_ENABLE
    /* write the file */
    log_file_write(log_buf, log_len);
#endif

    /* unlock output */
    log_output_unlock();
}

/**
 * get format enabled
 *
 * @param level level
 * @param set format set
 *
 * @return enable or disable
 */
static bool get_fmt_enabled(uint8_t level, size_t set)
{
    LOG_CHECK(level > LOG_LVL_VERBOSE, return false);

    if (g_log.enabled_fmt_set[level] & set) {
        return true;
    } else {
        return false;
    }
}

/**
 * enable or disable logger output lock
 * @note disable this lock is not recommended except you want output system exception log
 *
 * @param enabled true: enable  false: disable
 */
void log_output_lock_enabled(bool enabled)
{
    g_log.output_lock_enabled = enabled;
    /* it will re-lock or re-unlock before output lock enable */
    if (g_log.output_lock_enabled) {
        if (!g_log.output_is_locked_before_disable && g_log.output_is_locked_before_enable) {
            /* the output lock is unlocked before disable, and the lock will unlocking after enable */
            log_port_output_lock();
        } else if (g_log.output_is_locked_before_disable && !g_log.output_is_locked_before_enable) {
            /* the output lock is locked before disable, and the lock will locking after enable */
            log_port_output_unlock();
        }
    }
}

/**
 * Set a hook function to EasyLogger assert. It will run when the expression is false.
 *
 * @param hook the hook function
 */
void log_assert_set_hook(void (*hook)(const char *expr, const char *func, size_t line))
{
    log_assert_hook = hook;
}

/**
 * find the log level
 * @note make sure the log level is output on each format
 *
 * @param log log buffer
 *
 * @return log level, found failed will return -1
 */
int log_find_lvl(const char *log)
{
    LOG_CHECK(log == NULL, return -1;);
    /* make sure the log level is output on each format */
    LOG_CHECK((g_log.enabled_fmt_set[LOG_LVL_ASSERT] & LOG_FMT_LVL) == 0, return -1;);
    LOG_CHECK((g_log.enabled_fmt_set[LOG_LVL_ERROR] & LOG_FMT_LVL) == 0, return -1;);
    LOG_CHECK((g_log.enabled_fmt_set[LOG_LVL_WARN] & LOG_FMT_LVL) == 0, return -1;);
    LOG_CHECK((g_log.enabled_fmt_set[LOG_LVL_INFO] & LOG_FMT_LVL) == 0, return -1;);
    LOG_CHECK((g_log.enabled_fmt_set[LOG_LVL_DEBUG] & LOG_FMT_LVL) == 0, return -1;);
    LOG_CHECK((g_log.enabled_fmt_set[LOG_LVL_VERBOSE] & LOG_FMT_LVL) == 0, return -1;);

#ifdef LOG_COLOR_ENABLE
    uint8_t i;
    size_t csi_start_len = strlen(CSI_START);
    for (i = 0; i < LOG_LVL_MAX; i++) {
        if (!strncmp(color_output_info[i], log + csi_start_len, strlen(color_output_info[i]))) {
            return i;
        }
    }
    /* found failed */
    return -1;
#else
    switch (log[0]) {
    case 'A':
        return LOG_LVL_ASSERT;
    case 'E':
        return LOG_LVL_ERROR;
    case 'W':
        return LOG_LVL_WARN;
    case 'I':
        return LOG_LVL_INFO;
    case 'D':
        return LOG_LVL_DEBUG;
    case 'V':
        return LOG_LVL_VERBOSE;
    default:
        return -1;
    }
#endif
}

/**
 * find the log tag
 * @note make sure the log tag is output on each format
 * @note the tag don't have space in it
 *
 * @param log log buffer
 * @param lvl log level, you can get it by @see log_find_lvl
 * @param tag_len found tag length
 *
 * @return log tag, found failed will return NULL
 */
const char *log_find_tag(const char *log, uint8_t lvl, size_t *tag_len)
{
    const char *tag = NULL, *tag_end = NULL;

    LOG_CHECK(log == NULL, return NULL;);
    LOG_CHECK(tag_len == NULL, return NULL;);
    LOG_CHECK(lvl >= LOG_LVL_MAX, return NULL;);
    /* make sure the log tag is output on each format */
    LOG_CHECK((g_log.enabled_fmt_set[lvl] & LOG_FMT_TAG) == 0, return NULL;);

#ifdef LOG_COLOR_ENABLE
    tag = log + strlen(CSI_START) + strlen(color_output_info[lvl]) + strlen(level_output_info[lvl]);
#else
    tag = log + strlen(level_output_info[lvl]);
#endif
    /* find the first space after tag */
    if ((tag_end = memchr(tag, ' ', LOG_FILTER_TAG_MAX_LEN)) != NULL) {
        *tag_len = tag_end - tag;
    } else {
        tag = NULL;
    }

    return tag;
}

/**
 * dump the hex format data to log
 *
 * @param name name for hex object, it will show on log header
 * @param width hex number for every line, such as: 16, 32
 * @param buf hex buffer
 * @param size buffer size
 */
void log_hexdump(const char *name, uint8_t width, uint8_t *buf, uint16_t size)
{
#define __is_print(ch) ((unsigned int)((ch) - ' ') < 127u - ' ')

    int i, j;
    int log_len = 0;
    char dump_string[8] = {0};
    int fmt_result;

    if (!g_log.init_ok) {
        return;
    }

    if (!g_log.output_enabled) {
        return;
    }

    /* level filter */
    if (LOG_LVL_DEBUG > g_log.filter.level) {
        return;
    } else if (!strstr(name, g_log.filter.tag)) { /* tag filter */
        return;
    }

    /* lock output */
    log_output_lock();

    for (i = 0; i < size; i += width) {
        /* package header */
        fmt_result = snprintf(log_buf, LOG_LINE_BUF_SIZE, "D/HEX %s: %04X-%04X: ", name, i, i + width - 1);
        /* calculate log length */
        if ((fmt_result > -1) && (fmt_result <= LOG_LINE_BUF_SIZE)) {
            log_len = fmt_result;
        } else {
            log_len = LOG_LINE_BUF_SIZE;
        }
        /* dump hex */
        for (j = 0; j < width; j++) {
            if (i + j < size) {
                snprintf(dump_string, sizeof(dump_string), "%02X ", buf[i + j]);
            } else {
                strncpy(dump_string, "   ", sizeof(dump_string));
            }
            log_len += log_strcpy(log_len, log_buf + log_len, dump_string);
            if ((j + 1) % 8 == 0) {
                log_len += log_strcpy(log_len, log_buf + log_len, " ");
            }
        }
        log_len += log_strcpy(log_len, log_buf + log_len, "  ");
        /* dump char for hex */
        for (j = 0; j < width; j++) {
            if (i + j < size) {
                snprintf(dump_string, sizeof(dump_string), "%c", __is_print(buf[i + j]) ? buf[i + j] : '.');
                log_len += log_strcpy(log_len, log_buf + log_len, dump_string);
            }
        }
        /* overflow check and reserve some space for newline sign */
        if (log_len + strlen(LOG_NEWLINE_SIGN) > LOG_LINE_BUF_SIZE) {
            log_len = LOG_LINE_BUF_SIZE - strlen(LOG_NEWLINE_SIGN);
        }
        /* package newline sign */
        log_len += log_strcpy(log_len, log_buf + log_len, LOG_NEWLINE_SIGN);
        /* do log output */
        log_port_output(log_buf, log_len);

#ifdef LOG_FILE_ENABLE
        /* write the file */
        log_file_write(log_buf, log_len);
#endif
    }
    /* unlock output */
    log_output_unlock();
}

/**
 * EasyLogger port initialize
 *
 * @return result
 */
static int log_port_init(void) {
    pthread_mutex_init(&output_lock, NULL);
    return 0;
}

/**
 * EasyLogger port deinitialize
 *
 */
static int log_port_deinit(void) {
    pthread_mutex_destroy(&output_lock);
    return 0;
}

/**
 * output log port interface
 *
 * @param log output of log
 * @param size log size
 */
static void log_port_output(const char *log, size_t size) {
    printf("%.*s", (int)size, log);
}

/**
 * output lock
 */
static void log_port_output_lock(void) {
    pthread_mutex_lock(&output_lock);
}

/**
 * output unlock
 */
static void log_port_output_unlock(void) {
    pthread_mutex_unlock(&output_lock);
}

/**
 * get current time interface
 *
 * @return current time
 */
static const char *log_port_get_time(void) {
    static char cur_system_time[32] = { 0 };

    time_t cur_t;
    struct tm cur_tm;
    struct timeval tv;
    char system_time[24] = { 0 };

    gettimeofday(&tv, NULL);
    cur_t = tv.tv_sec;
    localtime_r(&cur_t, &cur_tm);

    strftime(system_time, sizeof(system_time), "%Y-%m-%d %T", &cur_tm);
    snprintf(cur_system_time, sizeof(cur_system_time), "%s-%03ld", system_time, tv.tv_usec / 1000);

    return cur_system_time;
}

/**
 * get current process name interface
 *
 * @return current process name
 */
static const char *log_port_get_p_info(void) {
    static char cur_process_info[10] = { 0 };

    snprintf(cur_process_info, 10, "pid:%04d", getpid());

    return cur_process_info;
}

/**
 * get current thread name interface
 *
 * @return current thread name
 */
static const char *log_port_get_t_info(void) {
    static char cur_thread_info[10] = { 0 };

    snprintf(cur_thread_info, 10, "tid:%04ld", pthread_self());

    return cur_thread_info;
}

#ifdef LOG_FILE_ENABLE

/*  open lock  */
static int lock_open(void)
{
    int id, rc, i;
    union semun arg;
    struct semid_ds ds;

    id = semget(LOG_FILE_SEM_KEY, 1, 0666);
    if(unlikely(id == -1))
        goto err;

    arg.buf = &ds;

    for (i = 0; i < 10; i++) {
        rc = semctl(id, 0, IPC_STAT, arg);
        if (unlikely(rc == -1))
            goto err;

        if(ds.sem_otime != 0)
            break;

        usleep(10 * 1000);
    }

    if (unlikely(ds.sem_otime == 0))
        goto err;

    return id;
err:
    return -1;
}

/*  file port initialize */
static int log_file_port_init(void) {
    int id, rc;
    union semun arg;
    struct sembuf sembuf;

    id = semget(LOG_FILE_SEM_KEY, 1, IPC_CREAT | IPC_EXCL | 0666);
    if(likely(id == -1)) {
        id = lock_open();
        if (id == -1)
            return -1;
    } else {
        arg.val = 0;
        rc = semctl(id, 0, SETVAL, arg);
        if (rc == -1)
            return -1;

        sembuf.sem_num = 0;
        sembuf.sem_op = 1;
        sembuf.sem_flg = 0;

        rc = semop(id, &sembuf, 1);
        if (rc == -1)
            return -1;
    }

    semid = id;

    return 0;
}

/* file log lock */
static void inline log_file_port_lock(void)
{
    semid == -1 ? -1 : semop(semid, (struct sembuf *)&down, 1);
}

/* file log unlock */
static void inline log_file_port_unlock(void)
{
    semid == -1 ? -1 : semop(semid, (struct sembuf *)&up, 1);
}

/* file log deinit */
static void log_file_port_deinit(void)
{
    semid = -1;;
}

#endif
