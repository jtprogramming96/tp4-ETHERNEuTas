CC=gcc
CFLAGS=-Wall -Werror -g -pthread 
BIN=./bin

PROGS=server-chat

.PHONY: all
all: $(PROGS)

LIST=$(addprefix $(BIN)/, $(PROGS))

# Compilación del ejecutable desde el archivo en el subdirectorio 'servidor/'
server-chat: servidor/server-chat.c
	$(CC) -o $(BIN)/$@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(LIST)