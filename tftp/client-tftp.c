#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <arpa/inet.h>

int main(int argc, char* argv[])
{
    // quizás sea util enviar un arreglo de un tamaño máximo

    int udp_socket;
    struct sockaddr_in dest;

    // hacer 5 estructuras

    struct tftp_format {
        char opcode[2];   // Opcode de 2 caracteres
        char payload[8];  // Payload de 8 caracteres
    };

    if (argc != 5) {
        printf("Uso: %s IP PUERTO TYPE MSG6CHARS\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *dir = argv[1];
    char *port = argv[2];
    char *type = argv[3];
    char *msg = argv[4];

    struct tftp_format package;


    strncpy(package.opcode, type, 2);
    strncpy(package.payload, msg, 6);

    printf("Enviando: %s\n", package.opcode);
    printf("Enviando: %s\n", package.payload);

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&dest, 0, sizeof(struct sockaddr_in)); // limpiar basura del espacio de memoria
    dest.sin_family = AF_INET;                    // configurar parámetros de la estructura
    dest.sin_port = htons(atoi(port));
    dest.sin_addr.s_addr = inet_addr(dir);

    sendto(udp_socket, (void *) &package, sizeof(package) + 1, 0, 
            (struct sockaddr*) &dest, sizeof(dest)); // porqué lo castea a un tipo parecido?

    exit(EXIT_SUCCESS);
}