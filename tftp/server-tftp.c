#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_PAYLOAD_SIZE 512

struct tftp_packet {
    short opcode;       // Opcode en formato de red (big-endian)
    char payload[MAX_PAYLOAD_SIZE];
};

int main(int argc, char* argv[]) {
    int udp_socket;
    struct sockaddr_in client_addr, server_addr;
    socklen_t client_len = sizeof(client_addr);
    
    if (argc != 2) {
        printf("Uso: %s PUERTO\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *port = argv[1];

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_socket, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor TFTP escuchando en puerto %s...\n", port);

    while (1) {
        struct tftp_packet packet;
        ssize_t recv_len = recvfrom(udp_socket, &packet, sizeof(packet), 0,
                                 (struct sockaddr*)&client_addr, &client_len);
        
        if (recv_len < 0) {
            perror("Error al recibir paquete");
            continue;
        }

        short opcode = ntohs(packet.opcode);

        switch (opcode) {
            case 2: {  
                // WRQ (Write Request)
                printf("WRQ recibido\n");
                
                // Extraer nombre de archivo y modo
                char *filename = packet.payload;
                char *mode = filename + strlen(filename) + 1;
                
                printf("Cliente quiere escribir archivo: %s (modo: %s)\n", filename, mode);
                
                // Construir ruta completa: uploads/filename
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "uploads/%s", filename);

                // Crear carpeta si no existe (esto podrías moverlo antes del bucle principal si quieres hacerlo solo una vez)
                mkdir("uploads", 0755);

                // Verificar si el archivo ya existe en la carpeta uploads/
                if (access(full_path, F_OK) == 0) {
                    printf("Error: Archivo ya existe en uploads/\n");

                    struct tftp_packet error_packet;
                    error_packet.opcode = htons(5);  // Opcode de error
                    short error_code = htons(6);     // Código 6 = Archivo ya existe
                    char *error_msg = "File already exists";

                    memcpy(error_packet.payload, &error_code, 2);
                    strcpy(error_packet.payload + 2, error_msg);
                    error_packet.payload[2 + strlen(error_msg)] = 0;

                    sendto(udp_socket, &error_packet, 4 + strlen(error_msg) + 1, 0,
                        (struct sockaddr*)&client_addr, client_len);
                    break;
                }

                // Crear archivo
                int file_fd = open(full_path, O_WRONLY | O_CREAT, 0644);
                if (file_fd < 0) {
                    perror("Error al crear archivo");
                    break;
                }

                // Enviar ACK (block #0) para confirmar WRQ
                struct tftp_packet ack_packet;
                ack_packet.opcode = htons(4);  // ACK opcode
                short block_num = htons(0);    // Block number 0
                memcpy(ack_packet.payload, &block_num, 2);
                
                sendto(udp_socket, &ack_packet, 4, 0,
                      (struct sockaddr*)&client_addr, client_len);
                
                printf("ACK enviado, esperando datos...\n");
                
                // Dentro del case 1 (WRQ), después de enviar ACK del bloque 0...
                int expected_block = 1;

                while (1) {
                    struct {
                        short opcode;
                        short block;
                        char data[512];
                    } data_packet;

                    ssize_t data_len = recvfrom(udp_socket, &data_packet, sizeof(data_packet), 0,
                            (struct sockaddr*)&client_addr, &client_len);

                    if (data_len < 0) {
                        perror("Error al recibir DATA");
                        break;
                    }

                    short data_opcode = ntohs(data_packet.opcode);
                    short block_number = ntohs(data_packet.block);
                    if (data_opcode != 3 || block_number != expected_block) {
                        printf("Bloque inesperado o no es DATA. Opcode: %d, Bloque: %d (esperado: %d)\n",
                            data_opcode, block_number, expected_block);
                        continue;
                    }

                    // Escribir datos
                    ssize_t data_size = data_len - 4;
                    if (write(file_fd, data_packet.data, data_size) != data_size) {
                        perror("Error al escribir en el archivo");
                        break;
                    }

                    // Enviar ACK
                    struct tftp_packet ack_packet;
                    ack_packet.opcode = htons(4);
                    short ack_block = htons(block_number);
                    memcpy(ack_packet.payload, &ack_block, 2);
                    sendto(udp_socket, &ack_packet, 4, 0, (struct sockaddr*)&client_addr, client_len);
                    printf("ACK enviado para bloque %d\n", block_number);

                    expected_block++;

                    // Fin del archivo
                    if (data_size < 512)
                        break;
                }

                printf("Transferencia finalizada. Archivo guardado en '%s'\n", full_path);
                
                close(file_fd);
                break;
            }
            case 1:  // RRQ (Read Request)
                {
                printf("RRQ recibido\n");

                // Extraer nombre de archivo y modo
                char *filename = packet.payload;
                char *mode = filename + strlen(filename) + 1;

                printf("Cliente quiere leer archivo: %s (modo: %s)\n", filename, mode);

                // Ruta al archivo en uploads/
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "uploads/%s", filename);

                int file_fd = open(full_path, O_RDONLY);
                if (file_fd < 0) {
                    perror("Archivo no encontrado o no se pudo abrir");
                    struct tftp_packet error_packet;
                    error_packet.opcode = htons(5);  // ERROR
                    short error_code = htons(1);     // File not found
                    char *error_msg = "File not found";
                    memcpy(error_packet.payload, &error_code, 2);
                    strcpy(error_packet.payload + 2, error_msg);
                    error_packet.payload[2 + strlen(error_msg)] = 0;

                    sendto(udp_socket, &error_packet, 4 + strlen(error_msg) + 1, 0,
                        (struct sockaddr*)&client_addr, client_len);
                    break;
                }

                short block_number = 1;
                ssize_t bytes_read;
                char buffer[512];

                while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
                    struct {
                        short opcode;
                        short block;
                        char data[512];
                    } data_packet;

                    data_packet.opcode = htons(3);  // DATA
                    data_packet.block = htons(block_number);
                    memcpy(data_packet.data, buffer, bytes_read);

                    ssize_t sent_len = sendto(udp_socket, &data_packet, 4 + bytes_read, 0,
                                            (struct sockaddr*)&client_addr, client_len);

                    if (sent_len < 0) {
                        perror("Error al enviar paquete DATA");
                        break;
                    }

                    // Esperar ACK
                    struct tftp_packet ack_packet;
                    ssize_t ack_len = recvfrom(udp_socket, &ack_packet, sizeof(ack_packet), 0,
                                            (struct sockaddr*)&client_addr, &client_len);

                    if (ack_len < 0) {
                        perror("Error al recibir ACK");
                        break;
                    }

                    short ack_opcode = ntohs(ack_packet.opcode);
                    short ack_block;
                    memcpy(&ack_block, ack_packet.payload, 2);
                    ack_block = ntohs(ack_block);

                    if (ack_opcode != 4 || ack_block != block_number) {
                        printf("ACK inválido recibido. Esperado: %d, recibido: %d\n", block_number, ack_block);
                        break;
                    }

                    printf("ACK %d recibido\n", block_number);
                    block_number++;
                }

                printf("Transferencia de lectura finalizada\n");
                close(file_fd);
                break;
            }
            default:
                printf("Opcode no válido: %d\n", opcode);
        }
        printf("Servidor listo, esperando próximas conexiones...\n");
    }

    close(udp_socket);
    return 0;
}