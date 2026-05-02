#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../storage/page.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s data/usuarios.tbl\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    FILE *file = fopen(filename, "rb");

    if (!file) {
        perror("No se pudo abrir el archivo");
        return 1;
    }

    Page page;

    size_t read = fread(&page, sizeof(Page), 1, file);

    if (read != 1) {
        printf("No se pudo leer la página.\n");
        fclose(file);
        return 1;
    }

    fclose(file);

    printf("Archivo: %s\n", filename);
    printf("Page ID: %d\n", page.page_id);
    printf("Número de registros: %d\n\n", page.num_records);

    for (int i = 0; i < page.num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page.data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        printf("Registro %d:\n", i + 1);
        printf("  xmin: %d\n", record.mvcc.xmin);
        printf("  xmax: %d\n", record.mvcc.xmax);
        printf("  valor: %s\n\n", record.value);
    }

    return 0;
}