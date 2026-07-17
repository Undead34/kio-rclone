# Primeros pasos

KIO Rclone usa exactamente los remotos que rclone ya conoce. Primero asegúrate
de que rclone puede listar el remoto desde la terminal; después ábrelo desde
Dolphin.

## 1. Instala el paquete

En Arch Linux, desde el checkout del proyecto:

~~~bash
cd packaging/arch
makepkg -si
kbuildsycoca6 --noincremental
~~~

Si ya tenías una versión instalada, `pacman` la sustituye; no necesitas
desinstalarla primero.

## 2. Configura un remoto

Abre **Rclone Remotes** desde el menú de aplicaciones o ejecuta:

~~~bash
kio-rclone-config
~~~

Puedes:

- añadir Google Drive;
- reconectar un remoto existente;
- borrar únicamente su entrada de configuración;
- abrir el remoto seleccionado directamente en Dolphin;
- usar **Other Providers…** para el configurador normal de rclone.

> [!TIP]
> Antes de culpar a Dolphin, comprueba el remoto en rclone. Por ejemplo:
>
> ~~~bash
> rclone lsd 'Mi remoto:'
> ~~~

## 3. Ábrelo en Dolphin

Escribe esto en la barra de ubicación:

~~~text
rclone:/
~~~

Verás todos los remotos. Si el remoto se llama `Google Drive`, su raíz será:

~~~text
rclone:/Google%20Drive/
~~~

Dolphin se encarga de mostrar los espacios normalmente; el `%20` solo aparece
en una URL escrita a mano.

## 4. Tu primera copia

1. Entra en una carpeta de tu remoto.
2. Arrastra un archivo local, o copia y pega.
3. Mira la notificación de Dolphin: durante una subida verás la preparación,
   el progreso remoto y la confirmación final.
4. Cuando termine, pulsa F5 para pedir un listado actualizado al proveedor.

Para entender qué ocurre cuando pausas una copia, lee
[Transferencias](/transfers).

## Google Drive: hazlo bien desde el inicio

El Client ID compartido de rclone tiene cuota global y puede introducir esperas
o límites. Para una experiencia estable, configura tu propio OAuth de Google
Drive antes de mover muchos archivos:

[Crear tu proyecto de Google Cloud](/google-drive)
