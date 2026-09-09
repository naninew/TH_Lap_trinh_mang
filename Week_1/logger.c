#include <stdio.h>
#include <time.h>
#include "logger.h"

int log_activity(const char *filename, int function_code, const char *user_input, int is_success) {
    FILE *fp = fopen(filename, "a");
    if (fp == NULL) {
        return -1;
    }
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(fp, "[%02d/%02d/%04d %02d:%02d:%02d] $ %d $ %s $ %s\n",
            t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
            t->tm_hour, t->tm_min, t->tm_sec,
            function_code, user_input, is_success ? "+OK" : "-ERR");
    fclose(fp);
    return 0;
}
