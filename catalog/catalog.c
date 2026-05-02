#include "catalog.h"
#include "../storage/file_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static TableMetadata tables[MAX_TABLES];
static int table_count = 0;

/*static void trim(char *s) {
    if (!s) return;

    int start = 0;
    while (isspace((unsigned char)s[start])) start++;

    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }

    int len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}*/

DataType catalog_parse_type(const char *type_str) {
    if (!type_str) return TYPE_UNKNOWN;

    if (strncasecmp(type_str, "INT", 3) == 0) {
        return TYPE_INT;
    }

    if (strncasecmp(type_str, "FLOAT", 5) == 0) {
        return TYPE_FLOAT;
    }

    if (strncasecmp(type_str, "VARCHAR", 7) == 0) {
        return TYPE_VARCHAR;
    }

    return TYPE_UNKNOWN;
}

const char* catalog_type_to_string(DataType type) {
    switch (type) {
        case TYPE_INT:
            return "INT";
        case TYPE_FLOAT:
            return "FLOAT";
        case TYPE_VARCHAR:
            return "VARCHAR";
        default:
            return "UNKNOWN";
    }
}

TableMetadata* catalog_get_table(const char *table_name) {
    for (int i = 0; i < table_count; i++) {
        if (strcmp(tables[i].table_name, table_name) == 0) {
            return &tables[i];
        }
    }

    return NULL;
}

int catalog_table_exists(const char *table_name) {
    return catalog_get_table(table_name) != NULL;
}

static void catalog_save(void) {
    FILE *f = fopen("data/catalog.dat", "w");
    if (!f) return;

    fprintf(f, "TABLE_COUNT %d\n", table_count);

    for (int i = 0; i < table_count; i++) {
        TableMetadata *t = &tables[i];

        fprintf(f, "TABLE %s %d %d\n",
                t->table_name,
                t->num_columns,
                t->next_auto_id);

        for (int j = 0; j < t->num_columns; j++) {
            ColumnMetadata *c = &t->columns[j];

            fprintf(f,
                    "COLUMN %s %d %d %d %d %d %s %s\n",
                    c->column_name,
                    c->type,
                    c->varchar_size,
                    c->is_primary_key,
                    c->is_auto_increment,
                    c->is_foreign_key,
                    c->references_table[0] ? c->references_table : "-",
                    c->references_column[0] ? c->references_column : "-");
        }

        fprintf(f, "END_TABLE\n");
    }

    fclose(f);
}

void catalog_init(void) {
    table_count = 0;

    FILE *f = fopen("data/catalog.dat", "r");
    if (!f) return;

    char keyword[64];

    while (fscanf(f, "%63s", keyword) == 1) {
        if (strcmp(keyword, "TABLE_COUNT") == 0) {
            fscanf(f, "%d", &table_count);

            if (table_count > MAX_TABLES) {
                table_count = MAX_TABLES;
            }
        } else if (strcmp(keyword, "TABLE") == 0) {
            static int current = -1;
            current++;

            if (current >= MAX_TABLES) break;

            fscanf(f, "%63s %d %d",
                   tables[current].table_name,
                   &tables[current].num_columns,
                   &tables[current].next_auto_id);

            if (tables[current].num_columns > MAX_COLUMNS) {
                tables[current].num_columns = MAX_COLUMNS;
            }

            for (int j = 0; j < tables[current].num_columns; j++) {
                fscanf(f, "%63s", keyword);

                if (strcmp(keyword, "COLUMN") == 0) {
                    ColumnMetadata *c = &tables[current].columns[j];

                    fscanf(f,
                           "%63s %d %d %d %d %d %63s %63s",
                           c->column_name,
                           (int *)&c->type,
                           &c->varchar_size,
                           &c->is_primary_key,
                           &c->is_auto_increment,
                           &c->is_foreign_key,
                           c->references_table,
                           c->references_column);

                    if (strcmp(c->references_table, "-") == 0) {
                        c->references_table[0] = '\0';
                    }

                    if (strcmp(c->references_column, "-") == 0) {
                        c->references_column[0] = '\0';
                    }
                }
            }
        }
    }

    fclose(f);
}

int catalog_create_table(const char *table_name) {
    if (catalog_table_exists(table_name)) return 0;
    if (table_count >= MAX_TABLES) return -1;

    TableMetadata table;
    memset(&table, 0, sizeof(TableMetadata));

    strncpy(table.table_name, table_name, sizeof(table.table_name) - 1);
    table.num_columns = 1;
    table.next_auto_id = 1;

    strncpy(table.columns[0].column_name, "value", sizeof(table.columns[0].column_name) - 1);
    table.columns[0].type = TYPE_VARCHAR;
    table.columns[0].varchar_size = 255;

    tables[table_count++] = table;

    catalog_save();

    return fm_create_table_file(table_name);
}

int catalog_create_table_schema(TableMetadata *table) {
    if (!table) return -1;

    if (catalog_table_exists(table->table_name)) {
        return 0;
    }

    if (table_count >= MAX_TABLES) {
        return -1;
    }

    if (table->next_auto_id <= 0) {
        table->next_auto_id = 1;
    }

    tables[table_count++] = *table;

    catalog_save();

    return fm_create_table_file(table->table_name);
}

int catalog_remove_table(const char *table_name) {
    int found = 0;

    for (int i = 0; i < table_count; i++) {
        if (strcmp(tables[i].table_name, table_name) == 0) {
            found = 1;

            for (int j = i; j < table_count - 1; j++) {
                tables[j] = tables[j + 1];
            }

            table_count--;
            break;
        }
    }

    if (!found) {
        return -1;
    }

    catalog_save();
    return 0;
}

void catalog_list_tables(void) {
    printf("Tablas registradas:\n");

    for (int i = 0; i < table_count; i++) {
        printf("- %s\n", tables[i].table_name);
    }
}

void catalog_describe_table(const char *table_name) {
    TableMetadata *table = catalog_get_table(table_name);

    if (!table) {
        printf("Error: la tabla '%s' no existe.\n", table_name);
        return;
    }

    printf("Tabla: %s\n", table->table_name);
    printf("Columnas:\n");

    for (int i = 0; i < table->num_columns; i++) {
        ColumnMetadata *c = &table->columns[i];

        printf("  %s %s",
               c->column_name,
               catalog_type_to_string(c->type));

        if (c->type == TYPE_VARCHAR) {
            printf("(%d)", c->varchar_size);
        }

        if (c->is_auto_increment) {
            printf(" AUTO_INCREMENT");
        }

        if (c->is_primary_key) {
            printf(" PRIMARY KEY");
        }

        if (c->is_foreign_key) {
            printf(" FOREIGN KEY REFERENCES %s(%s)",
                   c->references_table,
                   c->references_column);
        }

        printf("\n");
    }
}