#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PUERTO 10000
#define BUFFER_SIZE 1024
#define MAX_CLIENTES 100

typedef struct {
    char nombre[50];
    int socket;
} Cliente;

Cliente clientes[MAX_CLIENTES];
int num_clientes = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* manejar_cliente(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    char nombre[50];

    // 1. Recibir nombre de usuario
    int bytes = recv(sock, nombre, sizeof(nombre) - 1, 0);
    if (bytes <= 0) {
        close(sock);
        return NULL;
    }
    nombre[bytes] = '\0';

    // 2. Registrar cliente
    pthread_mutex_lock(&mutex);
    if (num_clientes >= MAX_CLIENTES) {
        pthread_mutex_unlock(&mutex);
        send(sock, "Servidor lleno.\n", 16, 0);
        close(sock);
        return NULL;
    }
    clientes[num_clientes].socket = sock;
    strncpy(clientes[num_clientes].nombre, nombre, sizeof(clientes[num_clientes].nombre));
    num_clientes++;
    pthread_mutex_unlock(&mutex);

    printf("Cliente conectado: %s\n", nombre);
    send(sock, "Conectado al servidor.\n", 24, 0);

    // 3. Esperar mensajes
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';

        // Separar destino y mensaje
        char* destino = strtok(buffer, ":");
        char* mensaje = strtok(NULL, "");

        if (!destino || !mensaje) {
            send(sock, "Formato inválido. Usa destino:mensaje\n", 39, 0);
            continue;
        }

        // MODO ARCHIVO: detectar FILE:
        if (strncmp(mensaje, "FILE:", 5) == 0) {
            char* nombre_archivo = strtok(mensaje + 5, ":");
            char* str_tamano = strtok(NULL, ":");

            if (!nombre_archivo || !str_tamano) {
                send(sock, "Encabezado de archivo inválido.\n", 32, 0);
                continue;
            }

            int tamano_archivo = atoi(str_tamano);
            if (tamano_archivo <= 0) {
                send(sock, "Tamaño de archivo inválido.\n", 29, 0);
                continue;
            }

            // Buscar destinatario
            int socket_destino = -1;
            pthread_mutex_lock(&mutex);
            for (int i = 0; i < num_clientes; ++i) {
                if (strcmp(clientes[i].nombre, destino) == 0) {
                    socket_destino = clientes[i].socket;
                    break;
                }
            }
            pthread_mutex_unlock(&mutex);

            if (socket_destino == -1) {
                send(sock, "Usuario no encontrado.\n", 24, 0);
                continue;
            }

            // Notificar destinatario
            char aviso[BUFFER_SIZE];
            snprintf(aviso, sizeof(aviso), "[%s] 📁 Recibiendo archivo '%s' (%d bytes)\n", nombre, nombre_archivo, tamano_archivo);
            send(socket_destino, aviso, strlen(aviso), 0);
            send(sock, "📤 Enviando archivo...\n", 24, 0);

            // Reenviar los datos binarios en bloques
            int total_recibido = 0;
            while (total_recibido < tamano_archivo) {
                int restante = tamano_archivo - total_recibido;
                int tam_bloque = (restante < BUFFER_SIZE) ? restante : BUFFER_SIZE;
                int rec = recv(sock, buffer, tam_bloque, 0);
                if (rec <= 0) break;

                send(socket_destino, buffer, rec, 0);
                total_recibido += rec;
            }

            send(sock, "✅ Archivo enviado con éxito.\n", 30, 0);
            send(socket_destino, "\n✅ Archivo recibido.\n", 23, 0);
            continue;
        }

        // MODO TEXTO
        int encontrado = 0;
        pthread_mutex_lock(&mutex);
        for (int i = 0; i < num_clientes; ++i) {
            if (strcmp(clientes[i].nombre, destino) == 0) {
                char mensaje_final[BUFFER_SIZE];
                snprintf(mensaje_final, sizeof(mensaje_final), "[%s] %s\n", nombre, mensaje);
                send(clientes[i].socket, mensaje_final, strlen(mensaje_final), 0);
                encontrado = 1;
                break;
            }
        }
        pthread_mutex_unlock(&mutex);

        if (!encontrado) {
            send(sock, "Usuario no encontrado.\n", 24, 0);
        } else {
            send(sock, "Mensaje enviado.\n", 17, 0);
        }
    }

    // 4. Desconexión
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < num_clientes; ++i) {
        if (clientes[i].socket == sock) {
            clientes[i] = clientes[num_clientes - 1]; // Reemplazo rápido
            num_clientes--;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);

    close(sock);
    printf("Cliente %s desconectado.\n", nombre);
    return NULL;
}

int main() {
    int servidor_fd;
    struct sockaddr_in direccion;
    socklen_t addrlen = sizeof(direccion);

    servidor_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor_fd == -1) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(PUERTO);

    if (bind(servidor_fd, (struct sockaddr*)&direccion, sizeof(direccion)) < 0) {
        perror("Error en bind");
        close(servidor_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(servidor_fd, 10) < 0) {
        perror("Error en listen");
        close(servidor_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor iniciado en puerto %d.\n", PUERTO);
    printf("Escribí 'salir' para apagar el servidor.\n");

    // Bucle principal con select() para escuchar teclado
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);      // teclado
        FD_SET(servidor_fd, &readfds);       // socket servidor

        int max_fd = (servidor_fd > STDIN_FILENO) ? servidor_fd : STDIN_FILENO;

        int actividad = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (actividad < 0) {
            perror("Error en select");
            break;
        }

        // Salida por teclado
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char comando[BUFFER_SIZE];
            fgets(comando, sizeof(comando), stdin);
            comando[strcspn(comando, "\n")] = 0; // quitar \n

            if (strcmp(comando, "salir") == 0) {
                printf("Cerrando servidor...\n");
                break;
            }
        }

        // Nueva conexión
        if (FD_ISSET(servidor_fd, &readfds)) {
            int* nuevo_socket = malloc(sizeof(int));
            *nuevo_socket = accept(servidor_fd, (struct sockaddr*)&direccion, &addrlen);
            if (*nuevo_socket < 0) {
                perror("Error en accept");
                free(nuevo_socket);
                continue;
            }

            pthread_t tid;
            pthread_create(&tid, NULL, manejar_cliente, nuevo_socket);
            pthread_detach(tid);
        }
    }

    close(servidor_fd);
    return 0;
}
