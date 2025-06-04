#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include "../constants.h"
#include "../tftp_packets.h"
#include <pthread.h>
#include <signal.h>

struct tftp_packet
{
    short opcode; // Opcode en formato de red (big-endian)
    char payload[MAX_SIZE];
};

int udp_socket;

void *input_thread(void *arg)
{
    char c;
    while (1)
    {
        c = getchar();
        if (c == 'x')
        {
            printf("[!] Cerrando socket para simular fallo.\n");
            close(udp_socket); // o el socket del cliente si estás del otro lado
            break;
        }
    }
    return NULL;
}

void handle_wrq(int udp_socket, struct sockaddr_in client_addr, socklen_t client_addr_len, struct tftp_packet packet)
{
    // WRQ (Write Request) format: |opcode|fileName|0|netascii|0|
    printf("WRQ recibido\n");

    // Extraer nombre de archivo y modo
    char *filename = packet.payload;
    char *mode = filename + strlen(filename) + 1; // str considera la longitud hasta el primer fin de cadena "\0"

    printf("Cliente quiere escribir archivo: %s (modo: %s)\n", filename, mode);

    // Construir ruta completa: uploads/filename
    char full_path[1024];
    char temp_path[1024];

    snprintf(full_path, sizeof(full_path), "uploads/%s", filename);
    snprintf(temp_path, sizeof(temp_path), "uploads/.partial_%s", filename);

    // Crear carpeta si no existe (esto podrías moverlo antes del bucle principal si quieres hacerlo solo una vez)
    mkdir("uploads", 0755);

    // Verificar si el archivo ya existe en la carpeta uploads/ o si está en proceso de transferencia
    if (access(full_path, F_OK) == 0 || access(temp_path, F_OK) == 0)
    {
        printf("Error: Archivo ya existe en uploads/\n");

        struct tftp_packet error_packet;
        error_packet.opcode = htons(OPCODE_ERROR);
        short error_code = htons(TFTP_ERROR_FILE_EXISTS);
        char *error_msg = "File already exists";

        memcpy(error_packet.payload, &error_code, 2);
        strcpy(error_packet.payload + 2, error_msg);
        error_packet.payload[2 + strlen(error_msg)] = 0; // payload = |errCode|errMsg|0|

        sendto(udp_socket, &error_packet, 4 + strlen(error_msg) + 1, 0,
               (struct sockaddr *)&client_addr, client_addr_len);
        exit(0);
    }

    // Crear archivo
    int file_fd = open(temp_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (file_fd < 0)
    {
        perror("Error al crear el archivo temporal");
        exit(0);
    }

    // Enviar ACK (block #0) para confirmar WRQ
    struct tftp_packet ack_packet;
    ack_packet.opcode = htons(OPCODE_ACK);
    short block_num = htons(0); // Block number 0
    memcpy(ack_packet.payload, &block_num, 2);

    sendto(udp_socket, &ack_packet, 4, 0,
           (struct sockaddr *)&client_addr, client_addr_len);

    printf("ACK enviado, esperando datos...\n");

    int expected_block = 1;

    int retries = 0;
    while (retries < 3)
    {
        struct
        {
            short opcode;
            short block;
            char data[MAX_SIZE];
        } data_packet;

        ssize_t data_len = recvfrom(udp_socket, &data_packet, sizeof(data_packet), 0,
                                    (struct sockaddr *)&client_addr, &client_addr_len);

        if (data_len < 0)
        {
            perror("Timeout o error al recibir DATA");

            // reenviar el último ACK
            short last_block = htons(expected_block - 1);
            struct tftp_packet resend_ack;
            resend_ack.opcode = htons(OPCODE_ACK);
            memcpy(resend_ack.payload, &last_block, 2);
            sendto(udp_socket, &resend_ack, 4, 0,
                   (struct sockaddr *)&client_addr, client_addr_len);

            retries++;
            printf("Reintentando recibir bloque %d (intento %d)\n", expected_block, retries);
            continue;
        }

        retries = 0; // reiniciar en recepción exitosa

        short received_opcode = ntohs(data_packet.opcode);
        short received_block = ntohs(data_packet.block);

        if (received_opcode != OPCODE_DATA || expected_block != received_block)
        {
            printf("Bloque inesperado o no es DATA. Opcode: %d, Bloque: %d (esperado: %d)\n",
                   received_opcode, received_block, expected_block);
            continue;
        }

        // Escribir datos
        ssize_t data_size = data_len - 4;
        if (write(file_fd, data_packet.data, data_size) != data_size)
        {
            perror("Error al escribir en el archivo");
            exit(0);
        }

        // Enviar ACK
        struct tftp_packet ack_packet;
        ack_packet.opcode = htons(OPCODE_ACK);
        short ack_block = htons(received_block);
        memcpy(ack_packet.payload, &ack_block, 2);
        sendto(udp_socket, &ack_packet, 4, 0,
               (struct sockaddr *)&client_addr, client_addr_len);

        printf("ACK enviado para bloque %d\n", received_block);
        expected_block++;

        // Fin del archivo
        if (data_size < MAX_SIZE)
            exit(0);
    }

    if (retries == 3)
    {
        printf("Error: no se recibió el bloque %d después de 3 intentos. Abortando.\n", expected_block);
        close(file_fd);
        unlink(temp_path); // eliminar archivo parcial
        exit(0);
    }

    printf("Transferencia finalizada. Archivo guardado en '%s'\n", full_path);

    close(file_fd);
    rename(temp_path, full_path);
}

void handle_rrq(int udp_socket, struct sockaddr_in client_addr, socklen_t client_addr_len, struct tftp_packet packet)
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
    if (file_fd < 0)
    {
        perror("Archivo no encontrado o no se pudo abrir");
        struct tftp_packet error_packet; // payload = |errCode|errMsg|0|
        error_packet.opcode = htons(OPCODE_ERROR);
        short error_code = htons(TFTP_ERROR_FILE_NOT_FOUND);
        char *error_msg = "File not found";
        memcpy(error_packet.payload, &error_code, sizeof(error_code));
        strcpy(error_packet.payload + sizeof(error_code), error_msg);
        error_packet.payload[2 + strlen(error_msg)] = 0;

        sendto(udp_socket,
               &error_packet, sizeof(error_packet.opcode) + sizeof(error_code) + strlen(error_msg) + 1,
               0,
               (struct sockaddr *)&client_addr,
               client_addr_len);
        exit(0);
    }

    short block_number = 1;
    ssize_t bytes_read;
    char buffer[MAX_SIZE];

    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
    {
        struct
        {
            short opcode;
            short block;
            char data[MAX_SIZE];
        } data_packet;

        data_packet.opcode = htons(OPCODE_DATA);
        data_packet.block = htons(block_number);
        memcpy(data_packet.data, buffer, bytes_read);

        int retries = 0;
        while (retries < 3)
        {
            sendto(udp_socket,
                   &data_packet, sizeof(data_packet.opcode) + sizeof(data_packet.block) + bytes_read,
                   0,
                   (struct sockaddr *)&client_addr, client_addr_len);

            // Esperar ACK
            struct tftp_packet ack_packet;
            ssize_t ack_len = recvfrom(udp_socket,
                                       &ack_packet, sizeof(ack_packet),
                                       0,
                                       (struct sockaddr *)&client_addr, &client_addr_len);

            if (ack_len >= 0)
            {
                short opcode = ntohs(ack_packet.opcode);
                short recongnized_block;
                memcpy(&recongnized_block, ack_packet.payload, sizeof(short));
                recongnized_block = ntohs(recongnized_block);

                if (opcode == OPCODE_ACK && recongnized_block == block_number)
                {
                    printf("ACK %d recibido\n", block_number);
                    break;
                }
            }

            retries++;
            printf("Timeout esperando ACK de bloque %d (reintento %d)\n", block_number, retries);
        }

        if (retries == 3)
        {
            printf("No se recibió ACK para bloque %d. Abortando transferencia.\n", block_number);
            exit(0);
        }

        block_number++;
        // TODO: valida cuando el archivo queda corrupto
    }

    printf("Transferencia de lectura finalizada\n");
    close(file_fd);
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    struct sockaddr_in client_addr, server_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (argc != 2)
    {
        printf("Uso: %s PUERTO\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *port = argv[1];

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
    {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    pthread_t tid;
    pthread_create(&tid, NULL, input_thread, NULL);

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error en bind");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor TFTP escuchando en puerto %s...\n", port);

    // Evitar procesos zombis
    signal(SIGCHLD, SIG_IGN);

    while (1)
    {
        struct tftp_packet packet;
        ssize_t recv_len = recvfrom(udp_socket, &packet, sizeof(packet), 0,
                                    (struct sockaddr *)&client_addr, &client_addr_len);

        if (recv_len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No llegó ningún paquete en el timeout (ej: servidor esperando)
                continue;
            }
            else
            {
                perror("Error al recibir paquete");
                continue;
            }
        }
        short opcode = ntohs(packet.opcode);

        if (opcode == OPCODE_WRQ || opcode == OPCODE_RRQ) {
            pid_t pid = fork();

            if (pid < 0) {
                perror("Error al crear proceso hijo");
                continue;
            }

            if (pid == 0) {
                // Proceso hijo
                close(udp_socket); // Cerramos socket heredado del padre

                // Creamos un nuevo socket exclusivo del hijo
                int child_socket = socket(AF_INET, SOCK_DGRAM, 0);
                if (child_socket < 0) {
                    perror("Error al crear socket en proceso hijo");
                    exit(1);
                }

                // Asignar puerto dinámico (puerto 0)
                struct sockaddr_in child_addr;
                memset(&child_addr, 0, sizeof(child_addr));
                child_addr.sin_family = AF_INET;
                child_addr.sin_addr.s_addr = INADDR_ANY;
                int puerto_hijo;
                int intentos = 0;
                do {
                    puerto_hijo = 25002 + rand() % 19;  // 25002–25020
                    child_addr.sin_port = htons(puerto_hijo);
                    intentos++;
                } while (bind(child_socket, (struct sockaddr*)&child_addr, sizeof(child_addr)) < 0 && intentos < 10);

                if (intentos == 10) {
                    perror("No se pudo bindear a un puerto del rango 25002–25020");
                    close(child_socket);
                    exit(1);
                }

                // Timeout para evitar bloqueos eternos
                struct timeval timeout;
                timeout.tv_sec = 3;
                timeout.tv_usec = 0;
                setsockopt(child_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

                // Lógica de transferencia según tipo
                if (opcode == OPCODE_WRQ)
                    handle_wrq(child_socket, client_addr, client_addr_len, packet);
                else
                    handle_rrq(child_socket, client_addr, client_addr_len, packet);

                close(child_socket);
                exit(0);
            }
        }

        printf("Servidor listo, esperando próximas conexiones...\n");
    }

    close(udp_socket);
    return 0;
}