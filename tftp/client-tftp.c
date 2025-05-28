#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

struct tftp_format {
    short opcode;       // Opcode en formato de red (big-endian)
    char payload[512];  // Payload (nombre de archivo + modo)
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Uso: %s IP PUERTO\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *ip = argv[1];
    char *port = argv[2];
    char filename[256];
    char mode[] = "netascii";  // Modo de transferencia (octet o netascii)

    // Obtener el nombre del archivo
    printf("Ingrese nombre del archivo a escribir: ");
    scanf("%255s", filename);

    // Abrir archivo local primero
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Error al abrir archivo local");
        exit(EXIT_FAILURE);
    }

    // Recién ahora crear el socket
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        perror("Error al crear socket");
        fclose(fp);
        exit(EXIT_FAILURE);
    }


    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    // Construir paquete WRQ (Write Request)
    struct tftp_format wrq_packet;
    wrq_packet.opcode = htons(1);  // WRQ opcode = 1
    
    // Formato: filename\0mode\0
    int payload_len = snprintf(wrq_packet.payload, sizeof(wrq_packet.payload),
                              "%s%c%s%c", filename, 0, mode, 0);

    // Enviar WRQ al servidor
    ssize_t sent = sendto(udp_socket, &wrq_packet, 2 + payload_len, 0,
                         (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (sent < 0) {
        perror("Error al enviar WRQ");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    printf("WRQ enviado para archivo: %s\n", filename);

        // Esperar ACK del bloque 0
    struct tftp_format ack_packet;
    socklen_t server_len = sizeof(server_addr);
    ssize_t ack_len = recvfrom(udp_socket, &ack_packet, sizeof(ack_packet), 0,
                               (struct sockaddr*)&server_addr, &server_len);
    if (ack_len < 0) {
        perror("Error al recibir ACK");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    short ack_opcode = ntohs(ack_packet.opcode);
    short ack_block;
    memcpy(&ack_block, ack_packet.payload, 2);
    ack_block = ntohs(ack_block);

    if (ack_opcode != 4 || ack_block != 0) {
        printf("ACK inválido recibido\n");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    printf("ACK 0 recibido. Iniciando envío de datos...\n");


    short block_number = 1;
    char buffer[512];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        struct {
            short opcode;
            short block;
            char data[512];
        } data_packet;

        data_packet.opcode = htons(3);
        data_packet.block = htons(block_number);
        memcpy(data_packet.data, buffer, bytes_read);

        ssize_t data_packet_len = 4 + bytes_read;
        sendto(udp_socket, &data_packet, data_packet_len, 0,
               (struct sockaddr*)&server_addr, server_len);

        // Esperar ACK
        ack_len = recvfrom(udp_socket, &ack_packet, sizeof(ack_packet), 0,
                           (struct sockaddr*)&server_addr, &server_len);
        if (ack_len < 0) {
            perror("Error al recibir ACK");
            break;
        }

        memcpy(&ack_block, ack_packet.payload, 2);
        ack_block = ntohs(ack_block);

        if (ntohs(ack_packet.opcode) != 4 || ack_block != block_number) {
            printf("ACK inválido recibido. Esperado %d, recibido %d\n", block_number, ack_block);
            break;
        }

        printf("ACK %d recibido\n", block_number);
        block_number++;
    }

    fclose(fp);

    printf("Transferencia finalizada. Archivo '%s' enviado correctamente.\n", filename);

    close(udp_socket);
    return 0;
}