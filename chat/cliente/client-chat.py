# cliente.py
import socket
import threading

HOST = '192.168.40.129'
PORT = 10000

def recibir_mensajes(sock):
    while True:
        try:
            data = sock.recv(1024).decode()
            if not data:
                print("Conexión cerrada por el servidor.")
                break
            print("\n📥", data.strip(), "\n>> ", end="", flush=True)
        except:
            break

nombre = input("Tu nombre: ")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    s.sendall(nombre.encode())

    print("Conectado. Escribí 'usuario:mensaje' o 'salir' para salir.\n")

    # Iniciar hilo para recibir mensajes
    thread_recv = threading.Thread(target=recibir_mensajes, args=(s,), daemon=True)
    thread_recv.start()

    while True:
        entrada = input(">> ")
        if entrada.lower() == "salir":
            break
        s.sendall(entrada.encode())
