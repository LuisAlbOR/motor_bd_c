#include "wal.h"
#include <string.h>

FILE* wal_open(void) {
    return fopen("logs/wal.log", "ab+");
}

void wal_write(FILE *log_file, LogRecord record) {
    if (!log_file) return;
    fwrite(&record, sizeof(LogRecord), 1, log_file);
    fflush(log_file);
}

void wal_close(FILE *log_file) {
    if (log_file) fclose(log_file);
}
