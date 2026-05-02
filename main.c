#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>

#include "buffer/buffer_pool.h"
#include "transaction/transaction.h"
#include "transaction/recovery.h"
#include "transaction/mvcc.h"
#include "concurrency/lock_manager.h"
#include "catalog/catalog.h"
#include "query/executor.h"
#include "query/parser.h"
#include "nl/nl2sql.h"
#include "network/server.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>

//manejo de interrupciones 
//subirlo a docker y hacerle su dominio/subirlo a la nube


static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int sig) {
    if (sig == SIGTSTP) {
        stop_requested = 1;
        const char msg[] = "\nCtrl+Z detectado. Saliendo como EXIT...\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
}


static void ensure_dirs(void) {
    mkdir("data", 0755);
    mkdir("logs", 0755);
}





static void print_help(void) {
    printf("\nComandos disponibles:\n");
    printf("  CREATE TABLE;\n");
    printf("  INSERT INTO (dato) VALUES;\n");
    printf("  SELECT * FROM;\n");
    printf("  SHOW TABLES;\n");
    printf("  BEGIN; COMMIT; ROLLBACK;\n");
    printf("  HELP;\n");
    printf("  EXIT;\n\n");
}

static int is_blank(const char *s) {
    return s == NULL || strspn(s, " \n\r\t") == strlen(s);
}

static void process_input_commands(char *input) {
    char command[512];
    int j = 0;

    memset(command, 0, sizeof(command));

    for (int i = 0; input[i] != '\0'; i++) {
        if (j < (int)sizeof(command) - 1) {
            command[j++] = input[i];
        }

        if (input[i] == ';') {
            command[j] = '\0';

            if (!is_blank(command)) {
                if (parser_is_safe_sql(command)) {
                    execute_sql(command);
                } else {
                    printf("SQL rechazado por seguridad.\n");
                }
            }

            j = 0;
            memset(command, 0, sizeof(command));
        }
    }

    command[j] = '\0';

    if (!is_blank(command)) {
        if (parser_is_safe_sql(command)) {
            execute_sql(command);
        } else {
            printf("SQL rechazado por seguridad.\n");
        }
    }
}

int main(int argc, char **argv) {
    ensure_dirs();
    //signal ctrl+Z
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // importante: NO usar SA_RESTART
    sigaction(SIGTSTP, &sa, NULL);
    recovery_run();
    lock_manager_init();
    catalog_init();
    mvcc_init();

    BufferPool bp;
    buffer_init(&bp);

    Transaction tx;
    tx_init(&tx);

    executor_init(&bp, &tx);

    if (argc == 3 && strcmp(argv[1], "--server") == 0) {
        return server_start(atoi(argv[2]));
    }

    printf("Motor de Base de Datos en C iniciado.\n");
    print_help();

    char input[512];

   while (!stop_requested) {
    const char *db = executor_get_current_database();

    if (strcmp(db, "default") == 0) {
        printf("motor_db> ");
    } else {
        printf("%s> ", db);
    }
    //signal ctrl+z
    if (!fgets(input, sizeof(input), stdin)) {
    if (stop_requested || errno == EINTR) {
        break;
    }
    break;
}
    if (strncasecmp(input, "EXIT", 4) == 0) {
        break;
    }

    if (strncasecmp(input, "HELP", 4) == 0) {
        print_help();
        continue;
    }

    process_input_commands(input);
    
    }

    buffer_flush_all(&bp);
    printf("Motor finalizado.\n");

    return 0;
}