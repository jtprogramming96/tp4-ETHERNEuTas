#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

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
    char mode[] = "netascii";  // Modo de transferencia

    char action;
    printf("\nDesea leer (r) o escribir (w) un archivo? ");
    scanf(" %c", &action);

    printf("Ingrese nombre del archivo: ");
    scanf("%255s", filename);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    socklen_t server_len = sizeof(server_addr);

    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    if (action == 'w') {
        FILE *fp = fopen(filename, "rb");
        if (!fp) {
            perror("Error al abrir archivo local para lectura");
            close(udp_socket);
            exit(EXIT_FAILURE);
        }

        struct tftp_format wrq_packet;
        wrq_packet.opcode = htons(1);  // WRQ
        int payload_len = snprintf(wrq_packet.payload, sizeof(wrq_packet.payload),
                                   "%s%c%s%c", filename, 0, mode, 0);

        sendto(udp_socket, &wrq_packet, 2 + payload_len, 0,
               (struct sockaddr*)&server_addr, server_len);

        // Esperar ACK del bloque 0
        struct tftp_format ack_packet;
        socklen_t server_len = sizeof(server_addr);
        ssize_t ack_len = recvfrom(udp_socket, &ack_packet, sizeof(ack_packet), 0,
                                (struct sockaddr*)&server_addr, &server_len);

        short ack_block;
        memcpy(&ack_block, ack_packet.payload, 2);
        ack_block = ntohs(ack_block);

        if (ntohs(ack_packet.opcode) != 4 || ack_block != 0) {
            printf("ACK inválido\n");
            fclose(fp);
            close(udp_socket);
            exit(EXIT_FAILURE);
        }

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

            sendto(udp_socket, &data_packet, 4 + bytes_read, 0,
                   (struct sockaddr*)&server_addr, server_len);

            memcpy(&ack_block, ack_packet.payload, 2);
            ack_block = ntohs(ack_block);

            if (ntohs(ack_packet.opcode) != 4 || ack_block != block_number) {
                printf("ACK inválido\n");
                break;
            }

            printf("ACK %d recibido\n", block_number);
            block_number++;
        }

        fclose(fp);
        printf("Archivo '%s' enviado exitosamente.\n", filename);

    } else if (action == 'r') {
        char path[300];
        snprintf(path, sizeof(path), "downloads/%s", filename);

        FILE *check = fopen(path, "rb");
        if (check) {
            printf("El archivo '%s' ya existe. Cancelando.\n", filename);
            fclose(check);
            close(udp_socket);
            exit(EXIT_FAILURE);
        }

        mkdir("downloads", 0755);
        FILE *out = fopen(path, "wb");
        if (!out) {
            perror("No se pudo crear archivo de destino");
            close(udp_socket);
            exit(EXIT_FAILURE);
        }

        struct tftp_format rrq_packet;
        rrq_packet.opcode = htons(2); // RRQ
        int payload_len = snprintf(rrq_packet.payload, sizeof(rrq_packet.payload),
                                   "%s%c%s%c", filename, 0, mode, 0);

        sendto(udp_socket, &rrq_packet, 2 + payload_len, 0,
               (struct sockaddr*)&server_addr, server_len);

        short expected_block = 1;

        while (1) {
            struct {
                short opcode;
                short block;
                char data[512];
            } data_packet;

            ssize_t data_len = recvfrom(udp_socket, &data_packet, sizeof(data_packet), 0,
                                        (struct sockaddr*)&server_addr, &server_len);

            if (data_len < 0) {
                perror("Error al recibir DATA");
                break;
            }

            short opcode = ntohs(data_packet.opcode);
            short block_num = ntohs(data_packet.block);

            if (opcode != 3 || block_num != expected_block) {
                printf("Paquete DATA inválido\n");
                break;
            }

            fwrite(data_packet.data, 1, data_len - 4, out);

            struct tftp_format ack;
            ack.opcode = htons(4);
            short ack_block = htons(block_num);
            memcpy(ack.payload, &ack_block, 2);
            sendto(udp_socket, &ack, 4, 0, (struct sockaddr*)&server_addr, server_len);

            printf("ACK %d enviado\n", block_num);
            expected_block++;
            if (data_len - 4 < 512) break;
        }

        fclose(out);
        printf("Archivo '%s' descargado exitosamente a 'downloads/'\n", filename);
    } else {
        printf("Acción inválida. Use 'r' para leer o 'w' para escribir.\n");
    }

    close(udp_socket);
    return 0;
}
