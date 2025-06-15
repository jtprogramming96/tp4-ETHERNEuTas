# cliente.py
import os
import socket
import threading
import base64

HOST = '192.168.40.129'
PORT = 10000

def recibir_mensajes(sock):
    while True:
        try:
            data = sock.recv(1024).decode()
            if not data:
                print("Conexión cerrada por el servidor.")
                break

            if "¿Deseás recibirlo?" in data:
                respuesta = input("Aceptar archivo (SI/NO): ").strip().upper()

                # Extraer nombre de archivo y emisor del mensaje
                partes = data.strip().split()
                archivo = partes[5].strip('"')
                emisor = partes[0]

                if respuesta == "SI":
                    cmd = f"aceptar_archivo:{archivo}:{emisor}"
                else:
                    cmd = f"rechazar_archivo:{archivo}:{emisor}"

                sock.sendall(cmd.encode())
                continue  # ⛔️ No sigas con impresión normal

            print("\n📥", data.strip(), "\n>> ", end="", flush=True)
        except:
            break

def enviar_archivo(sock, archivo, destino):
    try:
        with open(archivo, "rb") as f:
            contenido = f.read()

        chunk_size = 800  # Cada fragmento máx ~1KB base64
        total = len(contenido)
        for offset in range(0, total, chunk_size):
            fragmento = contenido[offset:offset + chunk_size]
            b64 = base64.b64encode(fragmento).decode()
            msg = f"enviando_archivo:{archivo}:{destino}:{offset}:{b64}"
            sock.sendall(msg.encode())
        
        print(f"✅ Archivo '{archivo}' enviado a {destino}.\n")

    except FileNotFoundError:
        print(f"❌ Archivo '{archivo}' no encontrado.")

nombre = input("Tu nombre: ")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    s.sendall(nombre.encode())

    print("Conectado. Escribí 'usuario:mensaje' o comandos como:")
    print("   enviar_archivo <archivo> <usuario>")
    print("   salir")

    # Iniciar hilo para recibir mensajes    
    thread_recv = threading.Thread(target=recibir_mensajes, args=(s,), daemon=True)
    thread_recv.start()

    while True:
        entrada = input(">> ")
        if entrada.lower() == "salir":
            break
        if entrada.startswith("enviar_archivo "):
            try:
                _, archivo, destinatario = entrada.split()
                if not os.path.isfile(archivo):
                    print(f"❌ El archivo '{archivo}' no existe.")
                    continue

                tam_bytes = os.path.getsize(archivo)
                comando = f"enviar_archivo:{archivo}:{destinatario}:{tam_bytes}"
                s.sendall(comando.encode())

                enviar_archivo(s, archivo, destinatario)

            except ValueError:
                print("❌ Uso correcto: enviar_archivo <archivo> <usuario>")
            continue
                
        s.sendall(entrada.encode())
