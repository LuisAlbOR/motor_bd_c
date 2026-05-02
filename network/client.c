#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define CLIENT_BUFFER_SIZE 1024

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Uso: %s <ip_servidor> <puerto>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Conectado al servidor %s:%d\n", ip, port);
    printf("Escribe SQL o EXIT para salir.\n");

    while (1) {
        char input[CLIENT_BUFFER_SIZE];
        char response[CLIENT_BUFFER_SIZE];

        printf("cliente_db> ");

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        write(sock, input, strlen(input));

        memset(response, 0, sizeof(response));
        ssize_t n = read(sock, response, sizeof(response) - 1);

        if (n <= 0) {
            printf("Servidor desconectado.\n");
            break;
        }

        response[n] = '\0';
        printf("%s", response);

        if (strncmp(input, "EXIT", 4) == 0) {
            break;
        }
    }

    close(sock);
    return 0;
}