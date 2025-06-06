# Compilador y flags
CC = gcc
CFLAGS = -Wall -Wextra -O2

# Rutas a los archivos fuente
CLIENTE_SRC = cliente/client-tftp.c
SERVIDOR_SRC = servidor/server-tftp.c

# Ejecutables
CLIENTE_BIN = cliente/cliente
SERVIDOR_BIN = servidor/servidor

# Regla por defecto
all: $(CLIENTE_BIN) $(SERVIDOR_BIN)

# Cliente
$(CLIENTE_BIN): $(CLIENTE_SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Servidor
$(SERVIDOR_BIN): $(SERVIDOR_SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Limpieza
clean:
	rm -f $(CLIENTE_BIN) $(SERVIDOR_BIN)
