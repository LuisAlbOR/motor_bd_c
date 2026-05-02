#include "server.h"
#include "../query/executor.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <arpa/inet.h>

#define SERVER_BUFFER_SIZE 1024
#define SERVER_OUTPUT_SIZE 4096



int server_start(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("Servidor escuchando en puerto %d...\n", port);

    while (1) {
        int client = accept(server_fd, NULL, NULL);

        if (client < 0) {
            perror("accept");
            continue;
        }

        printf("Cliente conectado.\n");

        while (1) {
            char buffer[SERVER_BUFFER_SIZE];
            char output[SERVER_OUTPUT_SIZE];

            memset(buffer, 0, sizeof(buffer));
            memset(output, 0, sizeof(output));

            ssize_t n = read(client, buffer, sizeof(buffer) - 1);

            if (n <= 0) {
                printf("Cliente desconectado.\n");
                break;
            }

            buffer[n] = '\0';

            if (strncasecmp(buffer, "EXIT", 4) == 0) {
                const char *msg = "Conexion cerrada.\n";
                write(client, msg, strlen(msg));
                break;
            }

            printf("Consulta recibida: %s", buffer);

            executor_set_output_buffer(output, sizeof(output));
            execute_sql(buffer);
            executor_clear_output_buffer();

            if (strlen(output) > 0) {
                write(client, output, strlen(output));
            } else {
                const char *msg = "OK: consulta procesada por el servidor.\n";
                write(client, msg, strlen(msg));
            }
        }

        close(client);
    }

    close(server_fd);
    return 0;
}