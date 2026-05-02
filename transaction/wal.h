#ifndef WAL_H
#define WAL_H

#include <stdio.h>

typedef enum {
    LOG_BEGIN = 1,
    LOG_UPDATE = 2,
    LOG_COMMIT = 3,
    LOG_ABORT = 4,
    LOG_CREATE_TABLE = 5,
    LOG_DROP_TABLE = 6
} LogType;

typedef struct {
    int transaction_id;
    LogType type;
    char table_name[64];
    int page_id;
    char before_image[256];
    char after_image[256];
} LogRecord;

FILE* wal_open(void);
void wal_write(FILE *log_file, LogRecord record);
void wal_close(FILE *log_file);

#endif
