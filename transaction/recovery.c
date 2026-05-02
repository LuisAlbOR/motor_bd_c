#include "recovery.h"
#include "wal.h"
#include <stdio.h>

void recovery_run(void) {
    FILE *f = fopen("logs/wal.log", "rb");
    if (!f) {
        printf("Recuperación: no existe WAL previo.\n");
        return;
    }

    LogRecord r;
    int count = 0;
    while (fread(&r, sizeof(LogRecord), 1, f) == 1) count++;
    fclose(f);
    printf("Recuperación: WAL revisado, %d registros encontrados.\n", count);
}
