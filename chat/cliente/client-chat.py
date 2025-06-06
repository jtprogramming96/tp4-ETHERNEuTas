# cliente.py
import socket

HOST = '192.168.40.129'
PORT = 10000

nombre = input("Tu nombre: ")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    s.sendall(nombre.encode())

    print("Conectado. Escribí 'usuario:mensaje' o 'salir' para salir.\n")

    while True:
        entrada = input(">> ")
        if entrada.lower() == "salir":
            break
        s.sendall(entrada.encode())
        respuesta = s.recv(1024).decode()
        print("Servidor:", respuesta)