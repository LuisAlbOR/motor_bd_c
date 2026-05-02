#ifndef CATALOG_H
#define CATALOG_H

#define MAX_TABLES 64
#define MAX_COLUMNS 16
#define MAX_FOREIGN_KEYS 8

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_VARCHAR,
    TYPE_UNKNOWN
} DataType;

typedef struct {
    char column_name[64];
    DataType type;
    int varchar_size;

    int is_primary_key;
    int is_auto_increment;
    int is_foreign_key;

    char references_table[64];
    char references_column[64];
} ColumnMetadata;

typedef struct {
    char table_name[64];
    int num_columns;
    ColumnMetadata columns[MAX_COLUMNS];

    int next_auto_id;
} TableMetadata;

void catalog_init(void);

/* Compatibilidad con tu versión vieja */
int catalog_create_table(const char *table_name);

/* Nueva versión con columnas */
int catalog_create_table_schema(TableMetadata *table);

int catalog_table_exists(const char *table_name);
TableMetadata* catalog_get_table(const char *table_name);

void catalog_list_tables(void);
void catalog_describe_table(const char *table_name);

int catalog_remove_table(const char *table_name);

/* Utilidades para tipos */
DataType catalog_parse_type(const char *type_str);
const char* catalog_type_to_string(DataType type);

#endif