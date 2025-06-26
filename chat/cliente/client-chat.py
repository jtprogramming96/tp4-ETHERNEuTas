# cliente.py
import socket
import threading
import os

HOST = '192.168.0.102'
PORT = 10000

def enviar_archivo(sock, destinatario, ruta_archivo):
    if not os.path.isfile(ruta_archivo):
        print(f"❌ El archivo '{ruta_archivo}' no existe.")
        return

    nombre_archivo = os.path.basename(ruta_archivo)
    tamaño = os.path.getsize(ruta_archivo)

    # Enviar encabezado
    encabezado = f"{destinatario}:FILE:{nombre_archivo}:{tamaño}"
    sock.sendall(encabezado.encode())
    print(f"📤 Enviando '{nombre_archivo}' a {destinatario} ({tamaño} bytes)...")

    # Enviar contenido del archivo
    try:
        with open(ruta_archivo, "rb") as archivo:
            while True:
                bloque = archivo.read(1024)
                if not bloque:
                    break
                sock.sendall(bloque)
        print(f"✅ Archivo '{nombre_archivo}' enviado correctamente.")
    except Exception as e:
        print(f"❌ Error al enviar el archivo: {e}")

def recibir_mensajes(sock):
    buffer_texto = b""
    modo_archivo = False
    bytes_restantes = 0
    archivo_actual = None

    while True:
        try:
            if not modo_archivo:
                data = sock.recv(1024)
                if not data:
                    print("Conexión cerrada por el servidor.")
                    break

                buffer_texto += data

                # Intentamos extraer líneas completas
                while b"\n" in buffer_texto and not modo_archivo:
                    linea, buffer_texto = buffer_texto.split(b"\n", 1)
                    linea_str = linea.decode(errors="ignore").strip()
                    print("\n📥", linea_str, "\n>> ", end="", flush=True)

                    # Detectar aviso de archivo, formato esperado:  
                    # [emisor] 📁 Recibiendo archivo 'nombre.ext' (N bytes)
                    if "📁 Recibiendo archivo" in linea_str:
                        try:
                            # Extraer nombre y tamaño
                            # Ejemplo: [usuario] 📁 Recibiendo archivo 'nombre.ext' (12345 bytes)
                            partes = linea_str.split("'")
                            archivo_actual = partes[1]
                            tam_part = linea_str.split("(")[1]
                            bytes_restantes = int(tam_part.split(" ")[0])
                            modo_archivo = True
                            print(f"📥 Empezando recepción de archivo '{archivo_actual}' ({bytes_restantes} bytes)")
                            # Abrir archivo para escritura binaria
                            archivo_actual = open(archivo_actual, "wb")
                        except Exception as e:
                            print(f"❌ Error parseando aviso de archivo: {e}")
                            modo_archivo = False

            else:
                # Estamos en modo recepción archivo
                datos = sock.recv(min(1024, bytes_restantes))
                if not datos:
                    print("Conexión cerrada inesperadamente durante recepción de archivo.")
                    archivo_actual.close()
                    break

                archivo_actual.write(datos)
                bytes_restantes -= len(datos)

                if bytes_restantes <= 0:
                    archivo_actual.close()
                    print(f"\n📥 Archivo '{archivo_actual.name}' recibido correctamente.\n>> ", end="", flush=True)
                    modo_archivo = False
                    archivo_actual = None

        except Exception as e:
            print(f"\n❌ Error en recepción: {e}")
            break

nombre = input("Tu nombre: ")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    s.sendall(nombre.encode())

    print("Conectado. Escribí 'usuario:mensaje' o:\n  archivo destinatario ruta/del/archivo.ext\n  salir\n")

    # Iniciar hilo para recibir mensajes
    thread_recv = threading.Thread(target=recibir_mensajes, args=(s,), daemon=True)
    thread_recv.start()

    while True:
        entrada = input(">> ").strip()

        if entrada.lower() == "salir":
            break

        if entrada.startswith("archivo "):
            try:
                _, destino, ruta = entrada.split(maxsplit=2)
                enviar_archivo(s, destino, ruta)
            except ValueError:
                print("Formato incorrecto. Usá: archivo destinatario ruta/del/archivo")
            continue

        # Si no es archivo, enviar como mensaje normal
        s.sendall(entrada.encode())