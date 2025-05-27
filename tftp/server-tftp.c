#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

int main(int argc, char* argv[])
{
    // inicialmente estaría bueno después de hacer el servidor
    // crear un cliente que almenos mande el codeOp+payLoad y q el servidor
    // recibe e imprime algo en base al codeOp.

    // primero secuencialmente, luego concurrentemente: fork o hilos 

    int udp_socket;
    struct sockaddr_in dir;
    
    struct tftp_format {
        char opcode[2];   // Opcode de 2 caracteres
        char payload[8];  // Payload de 8 caracteres
    };
    
    struct tftp_format package;

    if (argc != 2) {
        printf("Uso: %s PUERTO\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *port = argv[1];

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&dir, 0, sizeof(struct sockaddr_in));
    dir.sin_family = AF_INET;
    dir.sin_port = htons(atoi(port));
    dir.sin_addr.s_addr = INADDR_ANY;

    bind(udp_socket, (struct sockaddr*) &dir, sizeof(dir));

    while (recv(udp_socket, (void *) &package, sizeof(package), 0)) {

        int opcode_number = atoi(package.opcode);

        switch (opcode_number) {
            case 1:
                printf("Opcode de escritura\n");
                break;
            case 2:
                printf("Opcode de lectura\n");
                break;
            case 3:
                printf("Opcode de datos\n");
                break;
            case 4:
                printf("Opcode de reconocimiento\n");
                break;
            case 5:
                printf("Opcode de error\n");
                break;
            default:
                printf("opcode no válido\n");
        }
    }

    exit(EXIT_SUCCESS);

    // son 2 bytes de opcode y 2 bytes de secuencia, luego el payload de (como máximo) 512. en total son 516 bytes.
    // como ejercicio: como es que sé que terminó el archivo?
}