/*
 * @Author: Ghazi
 * @Date: 2021-07-28 11:20:08
 * @Description: 
 * @FilePath: /log/demo/main.c
 * @LastEditTime: 2021-07-28 18:53:39
 * @LastEditors: Ghazi
 */

#define LOG_TAG    "main"
#define LOG_LVL    LOG_LVL_WARN

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "log.h"

static void test_log(void);

int main(void) {

    /* dynamic set enable or disable for output logs (true or false) */
    // log_set_output_enabled(false);
    /* dynamic set output logs's level (from LOG_LVL_ASSERT to LOG_LVL_VERBOSE) */
    // log_set_filter_lvl(LOG_LVL_DEBUG);
    /* dynamic set output logs's filter for tag */
    // log_set_filter_tag("main");
    /* dynamic set output logs's filter for keyword */
    // log_set_filter_kw("Hello");
    /* dynamic set output logs's tag filter */
    // log_set_filter_tag_lvl("main", LOG_FILTER_LVL_SILENT);

    log_set_file_output_enabled(true);
    test_log();

    return EXIT_SUCCESS;
}

void test_log(void) {
    uint8_t buf[256]= {0};
    int i = 0;

    for (i = 0; i < sizeof(buf); i++)
    {
        buf[i] = i;
    }
    while(true) {
        /* test log output for all level */
        log_raw("Hello World!\n");
        usleep(1000);
        log_a("Hello World!");
        usleep(1000);
        log_e("Hello World!");
        usleep(1000);
        log_w("Hello World!");
        usleep(1000);
        log_i("Hello World!");
        usleep(1000);
        log_d("Hello World!");
        usleep(1000);
        log_v("Hello World!");
        usleep(1000);
        log_hexdump("test", 16, buf, sizeof(buf));
        sleep(5);
    }
}
