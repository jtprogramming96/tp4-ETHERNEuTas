# cliente.py
import socket
import threading
import os

HOST = '192.168.0.102'
PORT = 10000
archivo_pendiente_confirmacion = None
sock_global = None  # para enviar desde el hilo principal si es necesario
bytes_restantes = 0
archivo_actual = None

def enviar_archivo(sock, destinatario, ruta_archivo):
    if not os.path.isfile(ruta_archivo):
        print(f"❌ El archivo '{ruta_archivo}' no existe.")
        return

    nombre_archivo = os.path.basename(ruta_archivo)
    tamaño = os.path.getsize(ruta_archivo)

    # Enviar encabezado
    encabezado = f"{destinatario}:FILE:{nombre_archivo}:{tamaño}"
    sock.sendall(encabezado.encode())
    print(f"📤 Esperando confirmación para enviar '{nombre_archivo}' ({tamaño} bytes)...")

    # Esperar respuesta: ACEPTADO o RECHAZADO
    try:
        respuesta = sock.recv(1024).decode().strip()
    except Exception as e:
        print(f"❌ Error al esperar confirmación del servidor: {e}")
        return

    if respuesta == "ACEPTADO":
        print(f"✅ Envío aceptado. Enviando archivo '{nombre_archivo}'...")

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
    elif respuesta == "RECHAZADO":
        print(f"🚫 El destinatario rechazó el archivo '{nombre_archivo}'.")
    else:
        print(f"⚠️ Respuesta desconocida del servidor: {respuesta}")

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

                    # Detectar aviso de archivo, formato esperado:  
                    # [emisor] 📁 Recibiendo archivo 'nombre.ext' (N bytes)
                    if "CONFIRMAR_ARCHIVO:" in linea_str:
                        try:
                            # Extraer datos
                            partes = linea_str.split("CONFIRMAR_ARCHIVO:")[1].split(":")
                            nombre_archivo = partes[0]
                            tamaño_archivo = int(partes[1])

                            # Preguntar al usuario si desea aceptar
                            print(f"\n📥 Solicitan enviarte el archivo '{nombre_archivo}' ({tamaño_archivo} bytes)")
                            print("✋ Escribí 'aceptar' o 'rechazar' para responder.")
                            global archivo_pendiente_confirmacion
                            archivo_pendiente_confirmacion = (nombre_archivo, tamaño_archivo)

                        except Exception as e:
                            print(f"❌ Error al procesar confirmación de archivo: {e}")
                        continue

                    print("\n📥", linea_str, "\n>> ", end="", flush=True)
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

        # Si hay un archivo pendiente de confirmación
        if archivo_pendiente_confirmacion:
            if entrada.lower() in ["aceptar", "s"]:
                s.sendall("CONFIRMACION:ACEPTAR".encode())
                nombre_archivo, tamaño_archivo = archivo_pendiente_confirmacion
                print(f"📥 Aceptaste el archivo '{nombre_archivo}'")
                archivo_actual = open(nombre_archivo, "wb")
                modo_archivo = True
                bytes_restantes = tamaño_archivo
                archivo_pendiente_confirmacion = None
                continue
            elif entrada.lower() in ["rechazar", "n"]:
                s.sendall("CONFIRMACION:RECHAZAR".encode())
                nombre_archivo, _ = archivo_pendiente_confirmacion
                print(f"❌ Rechazaste el archivo '{nombre_archivo}'")
                archivo_pendiente_confirmacion = None
                continue

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