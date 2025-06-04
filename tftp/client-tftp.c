#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../constants.h"
#include "../tftp_packets.h"

struct tftp_format {
    short opcode;       // Opcode en formato de red (big-endian)
    char payload[MAX_SIZE];  // Payload (nombre de archivo + modo)
};

int main(int argc, char* argv[]) {
    int error_ocurred = 0;

    if (argc != 3) {
        printf("Uso: %s IP PUERTO\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *ip = argv[1];
    char *port = argv[2];
    char filename[256];
    char mode[] = "netascii";  // Modo de transferencia

    char action;
    printf("\nDesea descargar (d) o subir (s) un archivo? ");
    scanf(" %c", &action);

    printf("Ingrese nombre del archivo: ");
    scanf("%255s", filename);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    socklen_t server_len = sizeof(server_addr);

    // Crear socket
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);

    struct timeval timeout;
    timeout.tv_sec = 3;  // segundos
    timeout.tv_usec = 0;
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (udp_socket < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    if (action == 's') {
        // Abrir archivo local primero
        FILE *fp = fopen(filename, "rb");
        if (!fp) {
            perror("Error al abrir archivo local");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(atoi(port));
        inet_pton(AF_INET, ip, &server_addr.sin_addr);

        // Construir paquete WRQ (Write Request)
        struct tftp_format wrq_packet;
        wrq_packet.opcode = htons(2);  // WRQ opcode = 2
        
        // Formato: filename\0mode\0
        int payload_len = snprintf(wrq_packet.payload, sizeof(wrq_packet.payload),
                                "%s%c%s%c", filename, 0, mode, 0);

        // Enviar WRQ al servidor
        ssize_t sent = sendto(udp_socket, &wrq_packet, 2 + payload_len, 0,
                            (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        if (sent < 0) {
            perror("Error al enviar WRQ");
            close(udp_socket);
            // TODO: cerrar archivo fp ?
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
            // TODO: cerrar archivo fp ?
            exit(EXIT_FAILURE);
        }

        short opcode = ntohs(ack_packet.opcode);
        short payload;
        memcpy(&payload, ack_packet.payload, 2);
        payload = ntohs(payload);

        if (opcode == OPCODE_ERROR) { // format: |opcode|errCode|msg|0|
            switch (payload)           // payload <- errCode
            {
                case 0:
                    char *error_msg = ack_packet.payload + 2;
                    printf("Error: %s.\n", error_msg); 
                    break;
                case 1:
                    printf("Archivo no encontrado.\n");
                    break;  
                case 2:
                    printf("Acceso restringido.\n"); 
                    break; 
                case 3:
                    printf("Disco lleno o asignacion excedida.\n");  
                    break;
                case 4:
                    printf("Operacion TFTP no permitida.\n");  
                    break;
                case 5:
                    printf("ID de transferencia desconocida.\n"); 
                    break; 
                case 6:
                    printf("El archivo ya existe.\n");  
                    break;
                case 7:
                    printf("No se encuentra dicho usuario.\n");  
                    break;
            }
            printf("Cliente finalizado.\n");
            close(udp_socket);
            // TODO: cerrar archivo fp ?
            exit(EXIT_FAILURE);
        }

        if (opcode != 4 || payload != 0) {
            // y opcode = 1, 2 o 3 no importan?
            close(udp_socket);
            // TODO: cerrar archivo fp ?
            exit(EXIT_FAILURE);
        }

        printf("Bloque 0 confirmado. Iniciando envío de datos...\n");
        short sent_block = 1;
        char buffer[MAX_SIZE];
        size_t bytes_read;

        while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            struct {
                short opcode;
                short block;
                char data[MAX_SIZE];
            } data_packet;

            data_packet.opcode = htons(OPCODE_DATA);
            data_packet.block = htons(sent_block);
            memcpy(data_packet.data, buffer, bytes_read);

            ssize_t data_packet_len = sizeof(data_packet.opcode) + sizeof(data_packet.block) + bytes_read;
            
            int retries;
            do {
                sendto(udp_socket, &data_packet, data_packet_len, 0,
                    (struct sockaddr*)&server_addr, server_len);

                ack_len = recvfrom(udp_socket, &ack_packet, sizeof(ack_packet), 0,
                                (struct sockaddr*)&server_addr, &server_len);

                if (ack_len >= 0) {
                    memcpy(&payload, ack_packet.payload, 2);
                    short received_block = ntohs(payload);

                    if (ntohs(ack_packet.opcode) == 4 && received_block == sent_block) {
                        printf("Bloque %d confirmado\n", sent_block);
                        break;
                    }
                } else {
                    perror("Error al recibir ACK");
                }

                retries++;
                printf("Timeout o ACK inválido, reintentando bloque %d (%d/3)\n", sent_block, retries);
            } while (retries < 3);

            if (retries == 3) {
                printf("No se pudo confirmar bloque %d. Cancelando.\n", sent_block);
                break;
            }

            memcpy(&payload, ack_packet.payload, 2);
            short received_block = ntohs(payload);

            if (ntohs(ack_packet.opcode) != 4 || received_block != sent_block) {
                printf("ACK inválido recibido. Bloque esperado %d, bloque recibido %d\n", sent_block, received_block);
                // TODO: cerrar fp?
                break;
            }

            printf("Bloque %d confirmado\n", sent_block);
            sent_block++;
        }

        fclose(fp);

        printf("Transferencia finalizada. Archivo '%s' enviado correctamente.\n", filename);

    } else if (action == 'd') {
        char path[300];
        char temp_path[300];
        
        snprintf(path, sizeof(path), "downloads/%s", filename);
        snprintf(temp_path, sizeof(temp_path), "downloads/.partial_%s", filename);

        FILE *check = fopen(path, "rb");
        if (check) {
            printf("El archivo '%s' ya existe. Cancelando.\n", filename);
            fclose(check);
            close(udp_socket);
            exit(EXIT_FAILURE);
        }

        mkdir("downloads", 0755);
        FILE *out = fopen(temp_path, "wb");
        if (!out) {
            perror("No se pudo crear archivo temporal");
            close(udp_socket);
            exit(EXIT_FAILURE);
        }

        struct tftp_format rrq_packet;
        rrq_packet.opcode = htons(OPCODE_RRQ);
        int payload_len = snprintf(rrq_packet.payload,   // payload= |fileName|0|mode|0|
                                    sizeof(rrq_packet.payload),
                                    "%s%c%s%c",
                                    filename,
                                    0,
                                    mode,
                                    0);

        sendto(udp_socket, &rrq_packet, sizeof(rrq_packet.opcode) + payload_len, 0,
               (struct sockaddr*)&server_addr, server_len);

        short expected_block = 1;

        while (1) {
            struct {
                short opcode;
                short block;
                char data[MAX_SIZE];
            } data_packet;

            int retries = 0;
                
            while (retries < 3) {

                ssize_t data_len = recvfrom(udp_socket, &data_packet, sizeof(data_packet), 0,
                    (struct sockaddr*)&server_addr, &server_len);

                if (data_len >= 0) {
                    short opcode = ntohs(data_packet.opcode);
                    short received_block = ntohs(data_packet.block);

                    if (opcode == OPCODE_DATA && received_block == expected_block) {
                        // Procesar el bloque
                        fwrite(data_packet.data, 1, data_len - 4, out);

                        struct tftp_format ack;
                        ack.opcode = htons(OPCODE_ACK);
                        short ack_block = htons(received_block);
                        memcpy(ack.payload, &ack_block, sizeof(ack_block));
                        sendto(udp_socket, &ack, 4, 0, (struct sockaddr*)&server_addr, server_len);

                        printf("ACK %d enviado\n", received_block);
                        expected_block++;

                        if (data_len - 4 < MAX_SIZE) {
                            printf("Último bloque recibido (menos de %d bytes)\n", MAX_SIZE);
                            fclose(out);
                            rename(temp_path, path);
                            printf("Archivo '%s' descargado exitosamente a 'downloads/'\n", filename);
                            close(udp_socket);
                            return 0;
                        }

                        break;
                    } else {
                        perror("Error al recibir DATA");
                        retries++;
                    }
                }

                // Reenviar último ACK para forzar reenvío
                if (expected_block > 1) {
                    struct tftp_format ack;
                    ack.opcode = htons(OPCODE_ACK);
                    short ack_block = htons(expected_block - 1);
                    memcpy(ack.payload, &ack_block, sizeof(ack_block));
                    sendto(udp_socket, &ack, 4, 0, (struct sockaddr*)&server_addr, server_len);
                }

                retries++;
                printf("Esperando DATA bloque %d (reintento %d)\n", expected_block, retries);
            }

            if (retries == 3) {
                printf("Error: no se recibió el bloque %d. Abortando.\n", expected_block);
                error_ocurred = 1;
                break;
            }
        }
        
        fclose(out);
        if(error_ocurred) {
            unlink(temp_path);
        }
        else {
            rename(temp_path, path);
            printf("Archivo '%s' descargado exitosamente a 'downloads/'\n", filename);
        }
    } else {
        printf("Acción inválida. Use 'd' para descargar o 's' para subir.\n");
    }

    close(udp_socket);
    return error_ocurred? EXIT_FAILURE : EXIT_SUCCESS;
}
