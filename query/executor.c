#include "executor.h"
#include "parser.h"
#include "../catalog/catalog.h"
#include "../storage/file_manager.h"
#include "../storage/page.h"
#include "../transaction/wal.h"
#include "../transaction/mvcc.h"
#include "../concurrency/lock_manager.h"
#include "../index/btree.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>

#define MAX_TABLE_INDEXES 32
static BufferPool *global_bp = NULL;
static Transaction *global_tx = NULL;
static MVCCSnapshot current_snapshot;
//static BPlusTree *global_index = NULL;

static char pending_table[64];
static char pending_value[256];
static int has_pending_insert = 0;
static int current_mvcc_tx = 0;
static char *output_buffer = NULL;
static int output_buffer_size = 0;
static char current_database[64] = "default";


typedef struct {
    char table_name[64];
    BPlusTree *tree;
} TableIndex;

static TableIndex table_indexes[MAX_TABLE_INDEXES];

static BPlusTree *get_index_for_table(const char *table) {
    if (!table) return NULL;

    for (int i = 0; i < MAX_TABLE_INDEXES; i++) {
        if (table_indexes[i].tree &&
            strcmp(table_indexes[i].table_name, table) == 0) {
            return table_indexes[i].tree;
        }
    }

    for (int i = 0; i < MAX_TABLE_INDEXES; i++) {
        if (!table_indexes[i].tree) {
            strncpy(table_indexes[i].table_name, table, sizeof(table_indexes[i].table_name) - 1);
            table_indexes[i].table_name[sizeof(table_indexes[i].table_name) - 1] = '\0';

            table_indexes[i].tree = btree_create();
            return table_indexes[i].tree;
        }
    }

    return NULL;
}

static void remove_tbl_extension(const char *filename, char *table_name, size_t size) {
    strncpy(table_name, filename, size - 1);
    table_name[size - 1] = '\0';

    char *dot = strstr(table_name, ".tbl");
    if (dot) {
        *dot = '\0';
    }
}

static void rebuild_index_for_table(const char *table) {
    if (!table || !global_bp) return;

    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);

    if (fd < 0) {
        return;
    }

    Page *page = buffer_fetch_page(global_bp, fd, 0);

    if (!page) {
        close(fd);
        return;
    }

    BPlusTree *index = get_index_for_table(table);

    if (!index) {
        buffer_unpin_page(global_bp, fd, 0, 0);
        buffer_invalidate_fd(global_bp, fd);
        close(fd);
        return;
    }

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        int key = atoi(record.value);

        if (key > 0 && record.mvcc.xmax == 0) {
            btree_insert(index, key, 0, i);
        }
    }

    buffer_unpin_page(global_bp, fd, 0, 0);
    buffer_invalidate_fd(global_bp, fd);
    close(fd);
}

static void rebuild_all_indexes_from_disk(void) {
    DIR *dir = opendir("data");

    if (!dir) {
        return;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".tbl")) {
            char table_name[64];

            remove_tbl_extension(entry->d_name, table_name, sizeof(table_name));

            rebuild_index_for_table(table_name);

            printf("Índice B+ Tree reconstruido para tabla: %s\n", table_name);
        }
    }

    closedir(dir);
}

void executor_set_output_buffer(char *buffer, int size) {
    output_buffer = buffer;
    output_buffer_size = size;

    if (output_buffer && output_buffer_size > 0) {
        output_buffer[0] = '\0';
    }
}

void executor_clear_output_buffer(void) {
    output_buffer = NULL;
    output_buffer_size = 0;
}

void executor_append_output(const char *text) {
    if (!output_buffer || output_buffer_size <= 0 || !text) return;

    strncat(output_buffer, text, output_buffer_size - strlen(output_buffer) - 1);
}

void executor_printf(const char *format, ...) {
    va_list args;

    // 1. Imprimir en la consola del servidor (para depuración local en WSL)
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    // 2. Guardar en el buffer para enviar al cliente o Bridge de Node.js
    if (!output_buffer || output_buffer_size <= 0) return;

    size_t current_len = strlen(output_buffer);
    size_t remaining_space = output_buffer_size - current_len - 1;

    if (remaining_space <= 0) return;

    va_start(args, format);
    vsnprintf(output_buffer + current_len, remaining_space, format, args);
    va_end(args);
}



void executor_init(BufferPool *bp, Transaction *tx) {
    global_bp = bp;
    global_tx = tx;

    memset(table_indexes, 0, sizeof(table_indexes));

    rebuild_all_indexes_from_disk();
}

static void trim_semicolon(char *s) {
    size_t len = strlen(s);

    while (len > 0 &&
          (s[len - 1] == ';' ||
           s[len - 1] == '\n' ||
           s[len - 1] == '\r' ||
           s[len - 1] == ' ')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void write_insert_wal(const char *table, const char *value) {
    FILE *log = wal_open();

    LogRecord r;
    memset(&r, 0, sizeof(r));

    r.transaction_id = global_tx ? global_tx->transaction_id : 0;
    r.type = LOG_UPDATE;
    strncpy(r.table_name, table, sizeof(r.table_name) - 1);
    r.page_id = 0;
    strncpy(r.after_image, value, sizeof(r.after_image) - 1);

    wal_write(log, r);
    wal_close(log);
}

static void build_table_key(const char *table, char *out, size_t size) {
    snprintf(out, size, "%s/%s", current_database, table);
}

static int require_database_selected(void) {
    if (strcmp(current_database, "default") == 0) {
        printf("Error: no hay una base de datos seleccionada. Usa \\c nombre_base.\n");
        return 0;
    }

    return 1;
}

static void do_insert(const char *table, const char *value) {
    if (!catalog_table_exists(table)) {
        printf("Error: la tabla '%s' no existe.\n", table);
        return;
    }

    if (!global_bp) {
        printf("Error: Buffer Pool no inicializado.\n");
        return;
    }

    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);

    if (fd < 0) {
        printf("Error al abrir tabla.\n");
        return;
    }

    Page *page = buffer_fetch_page(global_bp, fd, 0);

    if (!page) {
        printf("Error: no se pudo cargar la página en el Buffer Pool.\n");
        buffer_invalidate_fd(global_bp, fd);
        close(fd);
        return;
    }

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        auto_tx = 1;
    }

    write_insert_wal(table, value);

    int slot_id = page->num_records;
    int page_id = 0;

    if (page_append_mvcc_record(page, value, tx_id) != 0) {
        printf("Error: página llena.\n");
        buffer_unpin_page(global_bp, fd, 0, 0);
        buffer_invalidate_fd(global_bp, fd);
        close(fd);

        if (auto_tx) {
            mvcc_abort(tx_id);
        }

        return;
    }

    int key = atoi(value);

    BPlusTree *table_index = get_index_for_table(table);

    if (key > 0 && table_index) {
        btree_insert(table_index, key, page_id, slot_id);
    }

    buffer_unpin_page(global_bp, fd, 0, 1);
    buffer_flush_page(global_bp, fd, 0);

    if (auto_tx) {
        mvcc_commit(tx_id);
    }
    buffer_invalidate_fd(global_bp, fd);
    close(fd);

    printf("Registro insertado en %s: %s\n", table, value);
}

static void do_select(const char *table) {
    char line[512];

    if (!catalog_table_exists(table)) {
        snprintf(line, sizeof(line), "Error: la tabla '%s' no existe.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    if (!global_bp) {
        snprintf(line, sizeof(line), "Error: Buffer Pool no inicializado.\n");
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    TableMetadata *meta = catalog_get_table(table);

    if (!meta) {
        snprintf(line, sizeof(line), "Error: metadata no encontrada para '%s'.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);

    if (fd < 0) {
        snprintf(line, sizeof(line), "Error al abrir tabla.\n");
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    Page *page = buffer_fetch_page(global_bp, fd, 0);

    if (!page) {
        snprintf(line, sizeof(line), "Error: no se pudo leer la página desde el Buffer Pool.\n");
        printf("%s", line);
        executor_append_output(line);
        buffer_invalidate_fd(global_bp, fd);
        close(fd);
        return;
    }

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;
    MVCCSnapshot snapshot;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        snapshot = mvcc_create_snapshot(tx_id);
        auto_tx = 1;
    } else {
        snapshot = current_snapshot;
    }

    snprintf(line, sizeof(line), "Contenido de %s:\n", table);
    printf("%s", line);
    executor_append_output(line);

    /* Encabezados */
    line[0] = '\0';

    for (int i = 0; i < meta->num_columns; i++) {
        strncat(line, meta->columns[i].column_name, sizeof(line) - strlen(line) - 1);

        if (i < meta->num_columns - 1) {
            strncat(line, " | ", sizeof(line) - strlen(line) - 1);
        }
    }

    strncat(line, "\n", sizeof(line) - strlen(line) - 1);
    printf("%s", line);
    executor_append_output(line);

    /* Separador */
    line[0] = '\0';

    for (int i = 0; i < meta->num_columns; i++) {
        strncat(line, "--------", sizeof(line) - strlen(line) - 1);

        if (i < meta->num_columns - 1) {
            strncat(line, "-+-", sizeof(line) - strlen(line) - 1);
        }
    }

    strncat(line, "\n", sizeof(line) - strlen(line) - 1);
    printf("%s", line);
    executor_append_output(line);

    int visible_count = 0;

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        if (mvcc_is_visible(record.mvcc, &snapshot)) {
            char value_copy[256];
            strncpy(value_copy, record.value, sizeof(value_copy) - 1);
            value_copy[sizeof(value_copy) - 1] = '\0';

            char row[512];
            row[0] = '\0';

            char *token = strtok(value_copy, "|");
            int col = 0;

            while (token && col < meta->num_columns) {
                strncat(row, token, sizeof(row) - strlen(row) - 1);

                if (col < meta->num_columns - 1) {
                    strncat(row, " | ", sizeof(row) - strlen(row) - 1);
                }

                token = strtok(NULL, "|");
                col++;
            }

            strncat(row, "\n", sizeof(row) - strlen(row) - 1);

            printf("%s", row);
            executor_append_output(row);

            visible_count++;
        }
    }

    if (visible_count == 0) {
        snprintf(line, sizeof(line), "Sin registros visibles.\n");
        printf("%s", line);
        executor_append_output(line);
    }

    if (auto_tx) {
        mvcc_commit(tx_id);
    }

    buffer_unpin_page(global_bp, fd, 0, 0);
    buffer_invalidate_fd(global_bp, fd);
    close(fd);
}

static void do_select_where_id(const char *table, int id) {
    char line[512];

    if (!catalog_table_exists(table)) {
        snprintf(line, sizeof(line), "Error: la tabla '%s' no existe.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    BPlusTree *table_index = get_index_for_table(table);

    if (!table_index) {
        snprintf(line, sizeof(line), "Error: índice B+ Tree no disponible para la tabla '%s'.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    BTreeRecordRef ref;

    if (!btree_search(table_index, id, &ref)) {
        snprintf(line, sizeof(line), "No encontrado con B+ Tree en tabla '%s' para id=%d.\n", table, id);
        printf("%s", line);
        executor_append_output(line);
        return;
    }
    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);

    if (fd < 0) {
        snprintf(line, sizeof(line), "Error al abrir tabla.\n");
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    Page *page = buffer_fetch_page(global_bp, fd, ref.page_id);

    if (!page) {
        snprintf(line, sizeof(line), "Error: no se pudo leer la página desde Buffer Pool.\n");
        printf("%s", line);
        executor_append_output(line);
        close(fd);
        return;
    }

    MVCCRecord record;

    memcpy(&record,
           page->data + (ref.slot_id * (int)sizeof(MVCCRecord)),
           sizeof(MVCCRecord));

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;
    MVCCSnapshot snapshot;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        snapshot = mvcc_create_snapshot(tx_id);
        auto_tx = 1;
    } else {
        snapshot = current_snapshot;
    }

    if (mvcc_is_visible(record.mvcc, &snapshot)) {
        snprintf(line, sizeof(line),
                 "Encontrado con B+ Tree en %s: %s  (xmin=%d, xmax=%d)\n",
                 table,
                 record.value,
                 record.mvcc.xmin,
                 record.mvcc.xmax);
    } else {
        snprintf(line, sizeof(line),
                 "Registro encontrado en índice de %s, pero no visible para esta transacción.\n",
                 table);
    }

    printf("%s", line);
    executor_append_output(line);

    if (auto_tx) {
        mvcc_commit(tx_id);
    }

    buffer_unpin_page(global_bp, fd, ref.page_id, 0);
    buffer_invalidate_fd(global_bp, fd);
    close(fd);
}

static void do_update_where_id(const char *table, int id, const char *new_value) {
    char line[512];

    if (!catalog_table_exists(table)) {
        snprintf(line, sizeof(line), "Error: la tabla '%s' no existe.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }
    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);
    if (fd < 0) {
        printf("Error al abrir tabla.\n");
        return;
    }

    Page *page = buffer_fetch_page(global_bp, fd, 0);
    if (!page) {
        close(fd);
        return;
    }

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        auto_tx = 1;
    }

    MVCCSnapshot snapshot = auto_tx ? mvcc_create_snapshot(tx_id) : current_snapshot;
    int found = 0;

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        if (atoi(record.value) == id && mvcc_is_visible(record.mvcc, &snapshot)) {
            record.mvcc.xmax = tx_id;

            memcpy(page->data + (i * (int)sizeof(MVCCRecord)),
                   &record,
                   sizeof(MVCCRecord));

            int new_slot = page->num_records;

            if (page_append_mvcc_record(page, new_value, tx_id) == 0) {
                BPlusTree *idx = get_index_for_table(table);
                int new_key = atoi(new_value);

                if (new_key > 0 && idx) {
                    btree_insert(idx, new_key, 0, new_slot);
                }

                found = 1;
            }

            break;
        }
    }

    buffer_unpin_page(global_bp, fd, 0, 1);
    buffer_flush_page(global_bp, fd, 0);
    buffer_invalidate_fd(global_bp, fd);
    close(fd);

    if (auto_tx) mvcc_commit(tx_id);

    if (found) {
        snprintf(line, sizeof(line), "UPDATE OK en %s: id=%d -> %s\n", table, id, new_value);
    } else {
        snprintf(line, sizeof(line), "No se encontró registro visible con id=%d.\n", id);
    }

    printf("%s", line);
    executor_append_output(line);
}

static void do_delete_where_id(const char *table, int id) {
    char line[512];

    if (!catalog_table_exists(table)) {
        snprintf(line, sizeof(line), "Error: la tabla '%s' no existe.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }
    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);
    if (fd < 0) return;

    Page *page = buffer_fetch_page(global_bp, fd, 0);
    if (!page) {
        close(fd);
        return;
    }

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        auto_tx = 1;
    }

    MVCCSnapshot snapshot = auto_tx ? mvcc_create_snapshot(tx_id) : current_snapshot;
    int found = 0;

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        if (atoi(record.value) == id && mvcc_is_visible(record.mvcc, &snapshot)) {
            record.mvcc.xmax = tx_id;

            memcpy(page->data + (i * (int)sizeof(MVCCRecord)),
                   &record,
                   sizeof(MVCCRecord));

            found = 1;
            break;
        }
    }

    buffer_unpin_page(global_bp, fd, 0, 1);
    buffer_flush_page(global_bp, fd, 0);
    buffer_invalidate_fd(global_bp, fd);
    close(fd);

    if (auto_tx) mvcc_commit(tx_id);

    if (found) {
        snprintf(line, sizeof(line), "DELETE OK en %s: id=%d\n", table, id);
    } else {
        snprintf(line, sizeof(line), "No se encontró registro visible con id=%d.\n", id);
    }

    printf("%s", line);
    executor_append_output(line);
}


static void do_drop_table(const char *table) {
    char line[512];
    char filename[128];

    if (!catalog_table_exists(table)) {
        snprintf(line, sizeof(line), "Error: la tabla '%s' no existe.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    snprintf(filename, sizeof(filename), "data/%s/%s.tbl", current_database, table);

    FILE *log = wal_open();

    LogRecord r;
    memset(&r, 0, sizeof(r));

    r.transaction_id = global_tx ? global_tx->transaction_id : 0;
    r.type = LOG_DROP_TABLE;
    r.page_id = -1;
    strncpy(r.table_name, table, sizeof(r.table_name) - 1);

    wal_write(log, r);
    wal_close(log);

    if (remove(filename) != 0) {
        snprintf(line, sizeof(line), "Error al eliminar archivo de tabla: %s\n", filename);
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    if (catalog_remove_table(table) != 0) {
        snprintf(line, sizeof(line), "Advertencia: tabla eliminada, pero no se pudo actualizar catálogo.\n");
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    for (int i = 0; i < MAX_TABLE_INDEXES; i++) {
        if (table_indexes[i].tree &&
            strcmp(table_indexes[i].table_name, table) == 0) {

            btree_free_tree(table_indexes[i].tree);
            table_indexes[i].tree = NULL;
            table_indexes[i].table_name[0] = '\0';
            break;
        }
    }

    snprintf(line, sizeof(line), "DROP OK: tabla eliminada %s.\n", table);
    printf("%s", line);
    executor_append_output(line);
}

static void do_join(const char *table1, const char *table2) {
    char line[512];

    if (!catalog_table_exists(table1) || !catalog_table_exists(table2)) {
        snprintf(line, sizeof(line), "Error: una de las tablas no existe.\n");
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    int fd1 = fm_open_table(table1);
    int fd2 = fm_open_table(table2);

    if (fd1 < 0 || fd2 < 0) {
        snprintf(line, sizeof(line), "Error al abrir tablas para JOIN.\n");
        printf("%s", line);
        executor_append_output(line);

        if (fd1 >= 0) close(fd1);
        if (fd2 >= 0) close(fd2);
        return;
    }

    Page *p1 = buffer_fetch_page(global_bp, fd1, 0);
    Page *p2 = buffer_fetch_page(global_bp, fd2, 0);

    if (!p1 || !p2) {
        snprintf(line, sizeof(line), "Error al leer páginas para JOIN.\n");
        printf("%s", line);
        executor_append_output(line);

        if (p1) buffer_unpin_page(global_bp, fd1, 0, 0);
        if (p2) buffer_unpin_page(global_bp, fd2, 0, 0);

        buffer_invalidate_fd(global_bp, fd1);
        buffer_invalidate_fd(global_bp, fd2);

        close(fd1);
        close(fd2);
        return;
    }

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;
    MVCCSnapshot snapshot;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        snapshot = mvcc_create_snapshot(tx_id);
        auto_tx = 1;
    } else {
        snapshot = current_snapshot;
    }

    snprintf(line, sizeof(line), "Resultado JOIN %s ⨝ %s:\n", table1, table2);
    printf("%s", line);
    executor_append_output(line);

    int count = 0;

    for (int i = 0; i < p1->num_records; i++) {
        MVCCRecord r1;
        memcpy(&r1, p1->data + (i * (int)sizeof(MVCCRecord)), sizeof(MVCCRecord));

        if (!mvcc_is_visible(r1.mvcc, &snapshot)) continue;

        int id1 = atoi(r1.value);

        for (int j = 0; j < p2->num_records; j++) {
            MVCCRecord r2;
            memcpy(&r2, p2->data + (j * (int)sizeof(MVCCRecord)), sizeof(MVCCRecord));

            if (!mvcc_is_visible(r2.mvcc, &snapshot)) continue;

            int id2 = atoi(r2.value);

            if (id1 == id2) {
                snprintf(line, sizeof(line), "  %s | %s\n", r1.value, r2.value);
                printf("%s", line);
                executor_append_output(line);
                count++;
            }
        }
    }

    if (count == 0) {
        snprintf(line, sizeof(line), "  Sin coincidencias.\n");
        printf("%s", line);
        executor_append_output(line);
    }

    if (auto_tx) {
        mvcc_commit(tx_id);
    }

    buffer_unpin_page(global_bp, fd1, 0, 0);
    buffer_unpin_page(global_bp, fd2, 0, 0);

    buffer_invalidate_fd(global_bp, fd1);
    buffer_invalidate_fd(global_bp, fd2);

    close(fd1);
    close(fd2);
}

static void clean_token(char *s) {
    if (!s) return;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        memmove(s, s + 1, strlen(s));
    }

    int len = strlen(s);

    while (len > 0 &&
          (s[len - 1] == ' ' ||
           s[len - 1] == '\t' ||
           s[len - 1] == '\n' ||
           s[len - 1] == '\r' ||
           s[len - 1] == '\'' ||
           s[len - 1] == ';')) {
        s[len - 1] = '\0';
        len--;
    }

    if (s[0] == '\'') {
        memmove(s, s + 1, strlen(s));
    }
}

static int parse_create_table_schema(const char *q, TableMetadata *meta) {
    char table_name[64];
    char columns_text[512];

    memset(meta, 0, sizeof(TableMetadata));

    if (sscanf(q, "CREATE TABLE %63s (%511[^)])", table_name, columns_text) != 2) {
        return -1;
    }

    trim_semicolon(table_name);

    strncpy(meta->table_name, table_name, sizeof(meta->table_name) - 1);
    meta->next_auto_id = 1;

    char *token = strtok(columns_text, ",");

    while (token && meta->num_columns < MAX_COLUMNS) {
        clean_token(token);

        if (strncasecmp(token, "FOREIGN KEY", 11) == 0) {
            char local_col[64];
            char ref_table[64];
            char ref_col[64];

            if (sscanf(token,
                       "FOREIGN KEY (%63[^)]) REFERENCES %63[^ (](%63[^)])",
                       local_col,
                       ref_table,
                       ref_col) == 3) {
                clean_token(local_col);
                clean_token(ref_table);
                clean_token(ref_col);

                for (int i = 0; i < meta->num_columns; i++) {
                    if (strcmp(meta->columns[i].column_name, local_col) == 0) {
                        meta->columns[i].is_foreign_key = 1;
                        strncpy(meta->columns[i].references_table,
                                ref_table,
                                sizeof(meta->columns[i].references_table) - 1);
                        strncpy(meta->columns[i].references_column,
                                ref_col,
                                sizeof(meta->columns[i].references_column) - 1);
                        break;
                    }
                }
            }

            token = strtok(NULL, ",");
            continue;
        }

        ColumnMetadata *col = &meta->columns[meta->num_columns];

        char col_name[64] = "";
        char col_type[64] = "";

        if (sscanf(token, "%63s %63s", col_name, col_type) < 2) {
            token = strtok(NULL, ",");
            continue;
        }

        strncpy(col->column_name, col_name, sizeof(col->column_name) - 1);
        col->type = catalog_parse_type(col_type);

        if (strncasecmp(col_type, "VARCHAR", 7) == 0) {
            int size = 0;
            sscanf(col_type, "VARCHAR(%d)", &size);
            sscanf(col_type, "varchar(%d)", &size);
            col->varchar_size = size > 0 ? size : 255;
        }

        if (strstr(token, "PRIMARY KEY") || strstr(token, "primary key")) {
            col->is_primary_key = 1;
        }

        if (strstr(token, "AUTO_INCREMENT") || strstr(token, "auto_increment")) {
            col->is_auto_increment = 1;
        }

        meta->num_columns++;
        token = strtok(NULL, ",");
    }

    return 0;
}

static int build_records_from_insert(const char *q, char *table) {
    char insert_columns[512];
    char insert_values[1024];

    if (sscanf(q,
               "INSERT INTO %63s (%511[^)]) VALUES %1023[^\n]",
               table,
               insert_columns,
               insert_values) != 3) {
        return -1;
    }

    trim_semicolon(table);

    TableMetadata *meta = catalog_get_table(table);

    if (!meta) {
        printf("Error: la tabla '%s' no existe.\n", table);
        return -1;
    }

    char *p = insert_values;

    while ((p = strchr(p, '(')) != NULL) {
        p++;

        char *end = strchr(p, ')');
        if (!end) break;

        char inside[512];
        size_t len = (size_t)(end - p);

        if (len >= sizeof(inside)) {
            len = sizeof(inside) - 1;
        }

        strncpy(inside, p, len);
        inside[len] = '\0';

        char final_values[MAX_COLUMNS][128];

        for (int i = 0; i < MAX_COLUMNS; i++) {
            final_values[i][0] = '\0';
        }

        for (int i = 0; i < meta->num_columns; i++) {
            if (meta->columns[i].is_auto_increment) {
                snprintf(final_values[i], sizeof(final_values[i]), "%d", meta->next_auto_id++);
            }
        }

        char cols_copy[512];
        char vals_copy[512];

        strncpy(cols_copy, insert_columns, sizeof(cols_copy) - 1);
        cols_copy[sizeof(cols_copy) - 1] = '\0';

        strncpy(vals_copy, inside, sizeof(vals_copy) - 1);
        vals_copy[sizeof(vals_copy) - 1] = '\0';

        char *save_cols = NULL;
        char *save_vals = NULL;

        char *col_token = strtok_r(cols_copy, ",", &save_cols);
        char *val_token = strtok_r(vals_copy, ",", &save_vals);

        while (col_token && val_token) {
            clean_token(col_token);
            clean_token(val_token);

            for (int i = 0; i < meta->num_columns; i++) {
                if (strcmp(meta->columns[i].column_name, col_token) == 0) {
                    strncpy(final_values[i], val_token, sizeof(final_values[i]) - 1);
                    final_values[i][sizeof(final_values[i]) - 1] = '\0';
                    break;
                }
            }

            col_token = strtok_r(NULL, ",", &save_cols);
            val_token = strtok_r(NULL, ",", &save_vals);
        }

        char record[512];
        record[0] = '\0';

        for (int i = 0; i < meta->num_columns; i++) {
            if (i > 0) {
                strncat(record, "|", sizeof(record) - strlen(record) - 1);
            }

            strncat(record, final_values[i], sizeof(record) - strlen(record) - 1);
        }

        do_insert(table, record);

        p = end + 1;
    }

    return 0;
}

static int get_column_index(TableMetadata *meta, const char *column_name) {
    if (!meta || !column_name) return -1;

    for (int i = 0; i < meta->num_columns; i++) {
        if (strcmp(meta->columns[i].column_name, column_name) == 0) {
            return i;
        }
    }

    return -1;
}

static void split_record_values(const char *record_value, char values[MAX_COLUMNS][128]) {
    for (int i = 0; i < MAX_COLUMNS; i++) {
        values[i][0] = '\0';
    }

    char copy[512];
    strncpy(copy, record_value, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *token = strtok(copy, "|");
    int i = 0;

    while (token && i < MAX_COLUMNS) {
        strncpy(values[i], token, 127);
        values[i][127] = '\0';

        token = strtok(NULL, "|");
        i++;
    }
}

static void build_record_string(TableMetadata *meta, char values[MAX_COLUMNS][128], char *out, size_t out_size) {
    out[0] = '\0';

    for (int i = 0; i < meta->num_columns; i++) {
        if (i > 0) {
            strncat(out, "|", out_size - strlen(out) - 1);
        }

        strncat(out, values[i], out_size - strlen(out) - 1);
    }
}

static void apply_set_values(TableMetadata *meta, char values[MAX_COLUMNS][128], const char *set_text) {
    char copy[512];
    strncpy(copy, set_text, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *assignment = strtok(copy, ",");

    while (assignment) {
        char *eq = strchr(assignment, '=');

        if (eq) {
            *eq = '\0';

            char column[64];
            char value[128];

            strncpy(column, assignment, sizeof(column) - 1);
            column[sizeof(column) - 1] = '\0';

            strncpy(value, eq + 1, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';

            clean_token(column);
            clean_token(value);

            int index = get_column_index(meta, column);

            if (index >= 0) {
                strncpy(values[index], value, 127);
                values[index][127] = '\0';
            }
        }

        assignment = strtok(NULL, ",");
    }
}

static void do_update_columns_where_id(const char *table, const char *set_text, int id) {
    char line[512];

    if (!catalog_table_exists(table)) {
        snprintf(line, sizeof(line), "Error: la tabla '%s' no existe.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }

    TableMetadata *meta = catalog_get_table(table);

    if (!meta) {
        printf("Error: metadata no encontrada.\n");
        return;
    }

    int id_index = get_column_index(meta, "id");

    if (id_index < 0) {
        printf("Error: la tabla '%s' no tiene columna id.\n", table);
        return;
    }
    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);

    if (fd < 0) {
        printf("Error al abrir tabla.\n");
        return;
    }

    Page *page = buffer_fetch_page(global_bp, fd, 0);

    if (!page) {
        close(fd);
        return;
    }

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        auto_tx = 1;
    }

    MVCCSnapshot snapshot = auto_tx ? mvcc_create_snapshot(tx_id) : current_snapshot;
    int found = 0;

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        if (!mvcc_is_visible(record.mvcc, &snapshot)) {
            continue;
        }

        char values[MAX_COLUMNS][128];
        split_record_values(record.value, values);

        if (atoi(values[id_index]) == id) {
            record.mvcc.xmax = tx_id;

            memcpy(page->data + (i * (int)sizeof(MVCCRecord)),
                   &record,
                   sizeof(MVCCRecord));

            apply_set_values(meta, values, set_text);

            char new_record[512];
            build_record_string(meta, values, new_record, sizeof(new_record));

            int new_slot = page->num_records;

            if (page_append_mvcc_record(page, new_record, tx_id) == 0) {
                BPlusTree *idx = get_index_for_table(table);
                int new_key = atoi(values[id_index]);

                if (new_key > 0 && idx) {
                    btree_insert(idx, new_key, 0, new_slot);
                }

                found = 1;
            }

            break;
        }
    }

    buffer_unpin_page(global_bp, fd, 0, 1);
    buffer_flush_page(global_bp, fd, 0);
    buffer_invalidate_fd(global_bp, fd);
    close(fd);

    if (auto_tx) {
        mvcc_commit(tx_id);
    }

    if (found) {
        snprintf(line, sizeof(line), "UPDATE OK en %s WHERE id=%d.\n", table, id);
    } else {
        snprintf(line, sizeof(line), "No se encontró registro visible con id=%d.\n", id);
    }

    printf("%s", line);
    executor_append_output(line);
}

static void do_delete_all(const char *table) {
    char line[512];

    if (!catalog_table_exists(table)) {
        snprintf(line, sizeof(line), "Error: la tabla '%s' no existe.\n", table);
        printf("%s", line);
        executor_append_output(line);
        return;
    }
    char full_table[160];
    snprintf(full_table, sizeof(full_table), "%s/%s", current_database, table);
    int fd = fm_open_table(full_table);

    if (fd < 0) {
        printf("Error al abrir tabla.\n");
        return;
    }

    Page *page = buffer_fetch_page(global_bp, fd, 0);

    if (!page) {
        close(fd);
        return;
    }

    int tx_id = current_mvcc_tx;
    int auto_tx = 0;

    if (tx_id == 0) {
        tx_id = mvcc_begin();
        auto_tx = 1;
    }

    MVCCSnapshot snapshot = auto_tx ? mvcc_create_snapshot(tx_id) : current_snapshot;
    int deleted = 0;

    for (int i = 0; i < page->num_records; i++) {
        MVCCRecord record;

        memcpy(&record,
               page->data + (i * (int)sizeof(MVCCRecord)),
               sizeof(MVCCRecord));

        if (mvcc_is_visible(record.mvcc, &snapshot)) {
            record.mvcc.xmax = tx_id;

            memcpy(page->data + (i * (int)sizeof(MVCCRecord)),
                   &record,
                   sizeof(MVCCRecord));

            deleted++;
        }
    }

    buffer_unpin_page(global_bp, fd, 0, 1);
    buffer_flush_page(global_bp, fd, 0);
    buffer_invalidate_fd(global_bp, fd);
    close(fd);

    if (auto_tx) {
        mvcc_commit(tx_id);
    }

    snprintf(line, sizeof(line), "DELETE OK: %d registros eliminados de %s.\n", deleted, table);
    printf("%s", line);
    executor_append_output(line);
}


static void ensure_database_folder(const char *db) {
    char path[128];
    snprintf(path, sizeof(path), "data/%s", db);

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    system(cmd);
}

static void do_create_database(const char *db) {
    if (!db || strlen(db) == 0) {
        printf("Error: nombre de base de datos inválido.\n");
        return;
    }

    ensure_database_folder(db);
    printf("Base de datos creada: %s\n", db);
}

static void do_use_database(const char *db_name) {
    char path[128];

    snprintf(path, sizeof(path), "data/%s", db_name);

    DIR *dir = opendir(path);

    if (!dir) {
        printf("Error: la base de datos '%s' no existe.\n", db_name);
        return;
    }

    closedir(dir);

    strncpy(current_database, db_name, sizeof(current_database) - 1);
    current_database[sizeof(current_database) - 1] = '\0';

    printf("Usando base de datos: %s\n", current_database);
}

static void do_show_databases(void) {
    DIR *dir = opendir("data");

    if (!dir) {
        printf("No hay bases de datos.\n");
        return;
    }

    struct dirent *entry;
    struct stat st;

    printf("Bases de datos:\n");

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char fullpath[256];
        snprintf(fullpath, sizeof(fullpath), "data/%s", entry->d_name);

        if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("  %s\n", entry->d_name);
        }
    }

    closedir(dir);
}

const char *executor_get_current_database(void) {
    return current_database;
}


static void do_show_tables_current_database(void) {
    if (strcmp(current_database, "default") == 0) {
        printf("Error: no hay una base de datos seleccionada. Usa \\c nombre_base.\n");
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), "data/%s", current_database);

    DIR *dir = opendir(path);

    if (!dir) {
        printf("Error: no se pudo abrir la base de datos '%s'.\n", current_database);
        return;
    }

    struct dirent *entry;
    printf("Tablas en base de datos '%s':\n", current_database);

    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        char *dot = strstr(entry->d_name, ".tbl");

        if (dot) {
            char table_name[128];
            strncpy(table_name, entry->d_name, sizeof(table_name) - 1);
            table_name[sizeof(table_name) - 1] = '\0';

            dot = strstr(table_name, ".tbl");
            if (dot) *dot = '\0';

            printf("  %s\n", table_name);
            count++;
        }
    }

    if (count == 0) {
        printf("  No hay tablas.\n");
    }

    closedir(dir);
}


static void do_drop_database(const char *db_name) {
    if (!db_name || strlen(db_name) == 0) {
        printf("Error: nombre de base de datos inválido.\n");
        return;
    }

    if (strcmp(db_name, "default") == 0) {
        printf("Error: no puedes eliminar la base de datos default.\n");
        return;
    }

    if (strcmp(current_database, db_name) == 0) {
        printf("Error: no puedes eliminar la base de datos que estás usando. Usa \\qdb primero.\n");
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), "data/%s", db_name);

    DIR *dir = opendir(path);

    if (!dir) {
        printf("Error: la base de datos '%s' no existe.\n", db_name);
        return;
    }

    struct dirent *entry;
    char filepath[256];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        remove(filepath);
    }

    closedir(dir);

    if (rmdir(path) == 0) {
        printf("Base de datos eliminada: %s\n", db_name);
    } else {
        printf("Error: no se pudo eliminar la base de datos '%s'.\n", db_name);
    }
}


void execute_sql(const char *query) {
    if (!query || strlen(query) == 0) return;

    ParsedQuery parsed;

    char quick[512];
    strncpy(quick, query, sizeof(quick) - 1);
    quick[sizeof(quick) - 1] = '\0';
    trim_semicolon(quick);

    char dbname[64];

    if (sscanf(quick, "CREATE DATABASE %63s", dbname) == 1) {
        trim_semicolon(dbname);
        do_create_database(dbname);
        return;
    }

    if (sscanf(quick, "DROP DATABASE %63s", dbname) == 1) {
        trim_semicolon(dbname);
        do_drop_database(dbname);
        return;
    }

    
    if (sscanf(quick, "\\c %63s", dbname) == 1) {
        trim_semicolon(dbname);
        do_use_database(dbname);
        return;
    }

    if (strcmp(quick, "SHOW DATABASES") == 0) {
        do_show_databases();
        return;
    }

    if (strcmp(quick, "\\qdb") == 0) {
        strcpy(current_database, "default");
        ensure_database_folder(current_database);
        printf("Saliste de la base actual. Ahora estás en: default\n");
        return;
    }

    if (!parser_parse(query, &parsed)) {
        printf("Consulta SQL inválida o no segura.\n");
        return;
    }

    parser_print(&parsed);

    char q[512];
    strncpy(q, query, sizeof(q) - 1);
    q[sizeof(q) - 1] = '\0';

    trim_semicolon(q);

    lock_database();

    if (strncmp(q, "CREATE TABLE", 12) == 0 ||
    strncmp(q, "INSERT INTO", 11) == 0 ||
    strncmp(q, "SELECT", 6) == 0 ||
    strncmp(q, "UPDATE", 6) == 0 ||
    strncmp(q, "DELETE FROM", 11) == 0 ||
    strncmp(q, "DROP TABLE", 10) == 0 ||
    strncmp(q, "SHOW TABLES", 11) == 0) {

    if (!require_database_selected()) {
        unlock_database();
        return;
     }
    }

   if (strncmp(q, "BEGIN", 5) == 0) {
    tx_begin(global_tx);
    current_mvcc_tx = mvcc_begin();
    current_snapshot = mvcc_create_snapshot(current_mvcc_tx);

    printf("MVCC BEGIN tx=%d\n", current_mvcc_tx);
    unlock_database();
    return;
    }

   if (strncmp(q, "COMMIT", 6) == 0) {
    if (has_pending_insert) {
        do_insert(pending_table, pending_value);
        has_pending_insert = 0;
    }

    if (current_mvcc_tx != 0) {
        mvcc_commit(current_mvcc_tx);
        printf("MVCC COMMIT tx=%d\n", current_mvcc_tx);
        current_mvcc_tx = 0;
    }

    tx_commit(global_tx);

    if (global_bp) {
        buffer_flush_all(global_bp);
    }

    unlock_database();
    return;
}

   if (strncmp(q, "ROLLBACK", 8) == 0) {
    has_pending_insert = 0;

    if (current_mvcc_tx != 0) {
        mvcc_abort(current_mvcc_tx);
        printf("MVCC ROLLBACK tx=%d\n", current_mvcc_tx);
        current_mvcc_tx = 0;
    }

    tx_rollback(global_tx);
    unlock_database();
    return;
    }

    char table[64];

    if (sscanf(q, "DROP TABLE %63s", table) == 1) {
        trim_semicolon(table);
        do_drop_table(table);
        unlock_database();
        return;
    }

    if (strncmp(q, "CREATE TABLE", 12) == 0) {
    TableMetadata meta;

    if (strchr(q, '(')) {
        if (parse_create_table_schema(q, &meta) != 0) {
            executor_printf("Error al interpretar CREATE TABLE.\n");
            unlock_database();
            return;
        }

        if (catalog_create_table_schema(&meta) == 0) {
            FILE *log = wal_open();

            LogRecord r;
            memset(&r, 0, sizeof(r));

            r.transaction_id = global_tx ? global_tx->transaction_id : 0;
            r.type = LOG_CREATE_TABLE;
            r.page_id = -1;
            strncpy(r.table_name, meta.table_name, sizeof(r.table_name) - 1);

            wal_write(log, r);
            wal_close(log);

            executor_printf("Tabla creada con esquema: %s\n", meta.table_name);
            catalog_describe_table(meta.table_name);
        } else {
            executor_printf("No se pudo crear la tabla.\n");
        }

        unlock_database();
        return;
    }

    if (sscanf(q, "CREATE TABLE %63s", table) == 1) {
        trim_semicolon(table);

        if (catalog_create_table(table) == 0) {
            executor_printf("Tabla creada: %s\n", table);
        } else {
            executor_printf("No se pudo crear la tabla.\n");
        }

        unlock_database();
        return;
        }
    }

    
        char value[256];

    if (strncmp(q, "INSERT INTO", 11) == 0) {

    /* INSERT moderno con columnas, uno o varios registros */
    if (strchr(q, '(')) {
        if (build_records_from_insert(q, table) != 0) {
            executor_printf("Error al interpretar INSERT.\n");
            unlock_database();
            return;
        }

        unlock_database();
        return;
    }

    /* INSERT viejo compatible */
    if (sscanf(q, "INSERT INTO %63s VALUES %255[^\n]", table, value) == 2) {
        trim_semicolon(table);
        trim_semicolon(value);

        do_insert(table, value);

        unlock_database();
        return;
    }
}

    int where_id;
 if (strncmp(q, "UPDATE", 6) == 0) {
    char set_text[512];
    char *set_pos = strcasestr(q, " SET ");
    char *where_pos = strcasestr(q, " WHERE ");

    if (sscanf(q, "UPDATE %63s", table) == 1 && set_pos && where_pos) {
        int len = (int)(where_pos - (set_pos + 5));

        if (len > 0 && len < (int)sizeof(set_text)) {
            strncpy(set_text, set_pos + 5, len);
            set_text[len] = '\0';

            if (sscanf(where_pos, " WHERE id = %d", &where_id) == 1) {
                trim_semicolon(table);
                do_update_columns_where_id(table, set_text, where_id);
                unlock_database();
                return;
            }
        }
    }

    printf("UPDATE inválido. Usa: UPDATE tabla SET columna = valor WHERE id = N;\n");
    unlock_database();
    return;
}

if (sscanf(q, "DELETE FROM %63s WHERE id = %d", table, &where_id) == 2) {
    trim_semicolon(table);
    do_delete_where_id(table, where_id);
    unlock_database();
    return;
}

if (sscanf(q, "DELETE FROM %63s", table) == 1) {
    trim_semicolon(table);
    do_delete_all(table);
    unlock_database();
    return;
}

    char join_table[64];

if (sscanf(q, "SELECT * FROM %63s JOIN %63s", table, join_table) == 2) {
    trim_semicolon(table);
    trim_semicolon(join_table);
    do_join(table, join_table);
    unlock_database();
    return;
}
    if (sscanf(q, "SELECT * FROM %63s WHERE id = %d", table, &where_id) == 2) {
        trim_semicolon(table);
        do_select_where_id(table, where_id);
        unlock_database();
        return;
    }

    if (sscanf(q, "SELECT * FROM %63s", table) == 1) {
        trim_semicolon(table);
        do_select(table);
        unlock_database();
        return;
    }

    if (strncmp(q, "SHOW TABLES", 11) == 0) {
    do_show_tables_current_database();
    unlock_database();
    return;
    }

    executor_printf("Consulta no reconocida.\n");
    unlock_database();
}