
#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static void trim(char *s) {
    if (!s) return;

    int start = 0;
    while (isspace((unsigned char)s[start])) start++;

    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }

    int len = (int)strlen(s);

    while (len > 0 &&
          (isspace((unsigned char)s[len - 1]) ||
           s[len - 1] == ';')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void to_upper_copy(const char *src, char *dst, size_t size) {
    size_t i;

    for (i = 0; src[i] && i < size - 1; i++) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }

    dst[i] = '\0';
}

//static int contains_word_upper(const char *upper, const char *word) {
   // return strstr(upper, word) != NULL;
//}

static SqlOperator parse_operator(const char *op) {
    if (strcmp(op, "=") == 0) return OP_EQ;
    if (strcmp(op, "!=") == 0) return OP_NEQ;
    if (strcmp(op, "<>") == 0) return OP_NEQ;
    if (strcmp(op, ">") == 0) return OP_GT;
    if (strcmp(op, "<") == 0) return OP_LT;
    if (strcmp(op, ">=") == 0) return OP_GTE;
    if (strcmp(op, "<=") == 0) return OP_LTE;
    if (strcasecmp(op, "IN") == 0) return OP_IN;

    return OP_NONE;
}

static const char* op_to_string(SqlOperator op) {
    switch (op) {
        case OP_EQ: return "=";
        case OP_NEQ: return "!=";
        case OP_GT: return ">";
        case OP_LT: return "<";
        case OP_GTE: return ">=";
        case OP_LTE: return "<=";
        case OP_IN: return "IN";
        default: return "NONE";
    }
}

static const char* logic_to_string(SqlLogic logic) {
    switch (logic) {
        case LOGIC_AND: return "AND";
        case LOGIC_OR: return "OR";
        default: return "NONE";
    }
}

int parser_is_safe_sql(const char *query) {
    if (!query) return 0;

    char upper[512];
    to_upper_copy(query, upper, sizeof(upper));

    const char *blocked[] = {
        "ALTER",
        "GRANT",
        "REVOKE",
        "EXEC",
        "EXECUTE",
        "ATTACH",
        "DETACH",
        "PRAGMA",
        "--",
        "/*",
        "*/",
        NULL
    };

    for (int i = 0; blocked[i]; i++) {
        if (strstr(upper, blocked[i])) {
            return 0;
        }
    }

    return 1;
}

static int split_list(char *text, char items[][128], int max_items) {
    int count = 0;
    char *token = strtok(text, ",");

    while (token && count < max_items) {
        trim(token);
        strncpy(items[count], token, 127);
        items[count][127] = '\0';

        count++;
        token = strtok(NULL, ",");
    }

    return count;
}

static int split_columns(char *text, char items[][64], int max_items) {
    int count = 0;
    char *token = strtok(text, ",");

    while (token && count < max_items) {
        trim(token);
        strncpy(items[count], token, 63);
        items[count][63] = '\0';

        count++;
        token = strtok(NULL, ",");
    }

    return count;
}

static void parse_conditions(char *where_text, ParsedQuery *out) {
    char *tokens[64];
    int token_count = 0;

    char buffer[512];
    strncpy(buffer, where_text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *token = strtok(buffer, " ");

    while (token && token_count < 64) {
        tokens[token_count++] = token;
        token = strtok(NULL, " ");
    }

    int i = 0;

    while (i + 2 < token_count && out->condition_count < PARSER_MAX_CONDITIONS) {
        SqlCondition *cond = &out->conditions[out->condition_count];

        strncpy(cond->left, tokens[i], sizeof(cond->left) - 1);
        cond->left[sizeof(cond->left) - 1] = '\0';

        cond->op = parse_operator(tokens[i + 1]);

        strncpy(cond->right, tokens[i + 2], sizeof(cond->right) - 1);
        cond->right[sizeof(cond->right) - 1] = '\0';

        cond->logic_to_next = LOGIC_NONE;

        i += 3;

        if (i < token_count) {
            if (strcasecmp(tokens[i], "AND") == 0) {
                cond->logic_to_next = LOGIC_AND;
                i++;
            } else if (strcasecmp(tokens[i], "OR") == 0) {
                cond->logic_to_next = LOGIC_OR;
                i++;
            }
        }

        out->condition_count++;
    }
}

static void parse_subquery(const char *query, ParsedQuery *out) {
    const char *start = strchr(query, '(');
    const char *end = strrchr(query, ')');

    if (start && end && end > start) {
        size_t len = (size_t)(end - start - 1);

        if (len < sizeof(out->subquery)) {
            strncpy(out->subquery, start + 1, len);
            out->subquery[len] = '\0';

            char upper[256];
            to_upper_copy(out->subquery, upper, sizeof(upper));

            if (strstr(upper, "SELECT")) {
                out->has_subquery = 1;
            }
        }
    }
}

static int parse_create(const char *query, ParsedQuery *out) {
    char table[64];

    if (sscanf(query, "CREATE TABLE %63s", table) == 1) {
        trim(table);
        strncpy(out->table, table, sizeof(out->table) - 1);
        out->type = SQL_CREATE;
        return 1;
    }

    return 0;
}

static int parse_insert(const char *query, ParsedQuery *out) {
    char table[64];
    const char *values_pos;

    if (sscanf(query, "INSERT INTO %63s", table) != 1) {
        return 0;
    }

    // Quitar "(nombre," si sscanf lo agarró mal
    char *paren = strchr(table, '(');
    if (paren) *paren = '\0';

    strncpy(out->table, table, sizeof(out->table) - 1);
    trim(out->table);

    values_pos = strcasestr(query, "VALUES");
    if (!values_pos) {
        return 0;
    }

    // Leer columnas del INSERT: INSERT INTO clientes (nombre, edad)
    const char *table_pos = strcasestr(query, out->table);
    if (table_pos) {
        const char *cols_open = strchr(table_pos + strlen(out->table), '(');
        const char *cols_close = strchr(table_pos + strlen(out->table), ')');

        if (cols_open && cols_close && cols_close < values_pos && cols_close > cols_open) {
            char cols_text[256];
            size_t cols_len = (size_t)(cols_close - cols_open - 1);

            if (cols_len >= sizeof(cols_text)) cols_len = sizeof(cols_text) - 1;

            strncpy(cols_text, cols_open + 1, cols_len);
            cols_text[cols_len] = '\0';

            out->column_count = split_columns(cols_text, out->columns, PARSER_MAX_COLUMNS);
        }
    }

    /*
      Leer SOLO la primera fila de VALUES.
      Ejemplo:
      VALUES ('Ana', 25), ('Luis', 30)
      Aquí el parser guardará:
      'Ana'
      25

      Las demás filas deben procesarse después en executor.c
      o con una función extra para INSERT múltiple.
    */
    const char *open = strchr(values_pos, '(');
    if (!open) return 0;

    int in_quote = 0;
    const char *close = NULL;

    for (const char *p = open + 1; *p; p++) {
        if (*p == '\'') {
            in_quote = !in_quote;
        }

        if (*p == ')' && !in_quote) {
            close = p;
            break;
        }
    }

    if (!close || close <= open) {
        return 0;
    }

    char values_text[512];
    size_t len = (size_t)(close - open - 1);

    if (len >= sizeof(values_text)) len = sizeof(values_text) - 1;

    strncpy(values_text, open + 1, len);
    values_text[len] = '\0';

    out->value_count = split_list(values_text, out->values, PARSER_MAX_COLUMNS);

    out->type = SQL_INSERT;
    return 1;
}

static int parse_select(const char *query, ParsedQuery *out) {
    char upper[512];
    to_upper_copy(query, upper, sizeof(upper));

    char *from_pos_upper = strstr(upper, " FROM ");
    if (!from_pos_upper) return 0;

    int from_index = (int)(from_pos_upper - upper);

    char columns_text[256];
    int col_len = from_index - 7;

    if (col_len <= 0) return 0;
    if (col_len >= (int)sizeof(columns_text)) col_len = sizeof(columns_text) - 1;

    strncpy(columns_text, query + 7, col_len);
    columns_text[col_len] = '\0';
    trim(columns_text);

    out->column_count = split_columns(columns_text, out->columns, PARSER_MAX_COLUMNS);

    const char *after_from = query + from_index + 6;

    char after_from_copy[512];
    strncpy(after_from_copy, after_from, sizeof(after_from_copy) - 1);
    after_from_copy[sizeof(after_from_copy) - 1] = '\0';

    char after_from_upper[512];
    to_upper_copy(after_from_copy, after_from_upper, sizeof(after_from_upper));

    char *join_pos = strstr(after_from_upper, " JOIN ");
    char *where_pos = strstr(after_from_upper, " WHERE ");

    if (join_pos) {
        int table_len = (int)(join_pos - after_from_upper);

        char table[64];
        if (table_len >= (int)sizeof(table)) table_len = sizeof(table) - 1;

        strncpy(table, after_from_copy, table_len);
        table[table_len] = '\0';
        trim(table);

        strncpy(out->table, table, sizeof(out->table) - 1);

        const char *join_real = after_from + table_len + 6;

        char join_part[256];
        strncpy(join_part, join_real, sizeof(join_part) - 1);
        join_part[sizeof(join_part) - 1] = '\0';

        char join_upper[256];
        to_upper_copy(join_part, join_upper, sizeof(join_upper));

        char *on_pos = strstr(join_upper, " ON ");
        if (on_pos) {
            int join_table_len = (int)(on_pos - join_upper);

            strncpy(out->join.join_table, join_part, join_table_len);
            out->join.join_table[join_table_len] = '\0';
            trim(out->join.join_table);

            char on_text[256];
            strncpy(on_text, join_part + join_table_len + 4, sizeof(on_text) - 1);
            on_text[sizeof(on_text) - 1] = '\0';

            char on_upper[256];
            to_upper_copy(on_text, on_upper, sizeof(on_upper));

            char *where_inside = strstr(on_upper, " WHERE ");
            if (where_inside) {
                *where_inside = '\0';
                on_text[where_inside - on_upper] = '\0';
            }

            sscanf(on_text, "%63s = %63s", out->join.join_left, out->join.join_right);
            trim(out->join.join_left);
            trim(out->join.join_right);

            out->join.has_join = 1;
        }
    } else {
        char table[64];

        if (where_pos) {
            int table_len = (int)(where_pos - after_from_upper);
            if (table_len >= (int)sizeof(table)) table_len = sizeof(table) - 1;

            strncpy(table, after_from_copy, table_len);
            table[table_len] = '\0';
        } else {
            strncpy(table, after_from_copy, sizeof(table) - 1);
            table[sizeof(table) - 1] = '\0';
        }

        trim(table);
        strncpy(out->table, table, sizeof(out->table) - 1);
    }

    if (where_pos) {
        int where_index = (int)(where_pos - after_from_upper);

        char where_text[512];
        strncpy(where_text, after_from_copy + where_index + 7, sizeof(where_text) - 1);
        where_text[sizeof(where_text) - 1] = '\0';
        trim(where_text);

        parse_conditions(where_text, out);
    }

    parse_subquery(query, out);

    out->type = SQL_SELECT;
    return 1;
}

static int parse_update(const char *query, ParsedQuery *out) {
    char table[64];

    if (sscanf(query, "UPDATE %63s", table) != 1) {
        return 0;
    }

    strncpy(out->table, table, sizeof(out->table) - 1);
    trim(out->table);

    char upper[512];
    to_upper_copy(query, upper, sizeof(upper));

    char *where_pos = strstr(upper, " WHERE ");
    if (where_pos) {
        int index = (int)(where_pos - upper);

        char where_text[512];
        strncpy(where_text, query + index + 7, sizeof(where_text) - 1);
        where_text[sizeof(where_text) - 1] = '\0';

        parse_conditions(where_text, out);
    }

    out->type = SQL_UPDATE;
    return 1;
}

static int parse_delete(const char *query, ParsedQuery *out) {
    char table[64];

    if (sscanf(query, "DELETE FROM %63s", table) != 1) {
        return 0;
    }

    strncpy(out->table, table, sizeof(out->table) - 1);
    trim(out->table);

    char upper[512];
    to_upper_copy(query, upper, sizeof(upper));

    char *where_pos = strstr(upper, " WHERE ");
    if (where_pos) {
        int index = (int)(where_pos - upper);

        char where_text[512];
        strncpy(where_text, query + index + 7, sizeof(where_text) - 1);
        where_text[sizeof(where_text) - 1] = '\0';

        parse_conditions(where_text, out);
    }

    out->type = SQL_DELETE;
    return 1;
}

int parser_parse(const char *query, ParsedQuery *out) {
    if (!query || !out) return 0;

    memset(out, 0, sizeof(ParsedQuery));

    strncpy(out->raw, query, sizeof(out->raw) - 1);
    out->raw[sizeof(out->raw) - 1] = '\0';
    trim(out->raw);

    if (!parser_is_safe_sql(out->raw)) {
        out->type = SQL_UNKNOWN;
        return 0;
    }

    char upper[512];
    to_upper_copy(out->raw, upper, sizeof(upper));

    if (strcmp(upper, "BEGIN") == 0) {
        out->type = SQL_BEGIN;
        return 1;
    }

    if (strcmp(upper, "COMMIT") == 0) {
        out->type = SQL_COMMIT;
        return 1;
    }

    if (strcmp(upper, "ROLLBACK") == 0) {
        out->type = SQL_ROLLBACK;
        return 1;
    }

    if (strcmp(upper, "SHOW TABLES") == 0) {
        out->type = SQL_SHOW_TABLES;
        return 1;
    }

    if (strncmp(upper, "DROP TABLE", 10) == 0) {
    char table[64];

    if (sscanf(out->raw, "DROP TABLE %63s", table) == 1) {
        trim(table);
        strncpy(out->table, table, sizeof(out->table) - 1);
        out->type = SQL_DROP;
        return 1;
    }
}

    if (strncmp(upper, "CREATE TABLE", 12) == 0) {
        return parse_create(out->raw, out);
    }

    if (strncmp(upper, "INSERT INTO", 11) == 0) {
        return parse_insert(out->raw, out);
    }

    if (strncmp(upper, "SELECT", 6) == 0) {
        return parse_select(out->raw, out);
    }

    if (strncmp(upper, "UPDATE", 6) == 0) {
        return parse_update(out->raw, out);
    }

    if (strncmp(upper, "DELETE FROM", 11) == 0) {
        return parse_delete(out->raw, out);
    }

    out->type = SQL_UNKNOWN;
    return 0;
}

void parser_print(const ParsedQuery *pq) {
    if (!pq) return;

    printf("====== PARSER RESULT ======\n");
    printf("RAW: %s\n", pq->raw);
    printf("TYPE: %d\n", pq->type);
    printf("TABLE: %s\n", pq->table);

    if (pq->column_count > 0) {
        printf("COLUMNS: ");
        for (int i = 0; i < pq->column_count; i++) {
            printf("%s ", pq->columns[i]);
        }
        printf("\n");
    }

    if (pq->value_count > 0) {
        printf("VALUES: ");
        for (int i = 0; i < pq->value_count; i++) {
            printf("%s ", pq->values[i]);
        }
        printf("\n");
    }

    if (pq->condition_count > 0) {
        printf("WHERE:\n");

        for (int i = 0; i < pq->condition_count; i++) {
            printf("  %s %s %s",
                   pq->conditions[i].left,
                   op_to_string(pq->conditions[i].op),
                   pq->conditions[i].right);

            if (pq->conditions[i].logic_to_next != LOGIC_NONE) {
                printf(" %s", logic_to_string(pq->conditions[i].logic_to_next));
            }

            printf("\n");
        }
    }

    if (pq->join.has_join) {
        printf("JOIN TABLE: %s\n", pq->join.join_table);
        printf("JOIN ON: %s = %s\n", pq->join.join_left, pq->join.join_right);
    }

    if (pq->has_subquery) {
        printf("SUBQUERY: %s\n", pq->subquery);
    }

    printf("===========================\n");
}