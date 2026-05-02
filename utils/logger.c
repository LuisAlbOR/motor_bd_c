#include "logger.h"
#include <stdio.h>
#include <time.h>

static void log_msg(const char *level, const char *msg) {
    FILE *f = fopen("logs/motor.log", "a");
    if (!f) return;
    time_t now = time(0);
    fprintf(f, "[%s] %ld %s\n", level, (long)now, msg);
    fclose(f);
}

void logger_info(const char *msg) { log_msg("INFO", msg); }
void logger_error(const char *msg) { log_msg("ERROR", msg); }
