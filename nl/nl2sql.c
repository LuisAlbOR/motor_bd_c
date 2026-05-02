#include "nl2sql.h"
#include <string.h>

const char* nl_to_sql(const char *natural_query) {
    if (!natural_query) return "";
    if (strstr(natural_query, "usuarios") || strstr(natural_query, "clientes")) {
        return "SELECT * FROM usuarios;";
    }
    return "SHOW TABLES;";
}
