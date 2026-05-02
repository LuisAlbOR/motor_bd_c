#ifndef PARSER_H
#define PARSER_H

#define PARSER_MAX_COLUMNS 10
#define PARSER_MAX_CONDITIONS 10

typedef enum {
    SQL_UNKNOWN = 0,
    SQL_CREATE,
    SQL_INSERT,
    SQL_SELECT,
    SQL_UPDATE,
    SQL_DELETE,
    SQL_SHOW_TABLES,
    SQL_BEGIN,
    SQL_COMMIT,
    SQL_ROLLBACK,
    SQL_TRUNCATE,
    SQL_DROP
} SqlType;

typedef enum {
    OP_NONE = 0,
    OP_EQ,
    OP_NEQ,
    OP_GT,
    OP_LT,
    OP_GTE,
    OP_LTE,
    OP_IN
} SqlOperator;

typedef enum {
    LOGIC_NONE = 0,
    LOGIC_AND,
    LOGIC_OR
} SqlLogic;

typedef struct {
    char left[64];
    SqlOperator op;
    char right[128];
    SqlLogic logic_to_next;
} SqlCondition;

typedef struct {
    int has_join;
    char join_table[64];
    char join_left[64];
    char join_right[64];
} SqlJoin;

typedef struct {
    SqlType type;

    char table[64];

    char columns[PARSER_MAX_COLUMNS][64];
    int column_count;

    char values[PARSER_MAX_COLUMNS][128];
    int value_count;

    SqlCondition conditions[PARSER_MAX_CONDITIONS];
    int condition_count;

    SqlJoin join;

    int has_subquery;
    char subquery[256];

    char raw[512];
} ParsedQuery;

int parser_is_safe_sql(const char *query);
int parser_parse(const char *query, ParsedQuery *out);
void parser_print(const ParsedQuery *pq);

#endif