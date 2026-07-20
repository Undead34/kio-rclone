---
description: Qué registra KIO Rclone, qué nunca guarda y cómo reportar un error sin filtrar datos de la nube.
---

# Logs y diagnóstico seguro

KIO Rclone debería poder depurarse sin recolectar datos privados de la nube.
No tiene servicio de telemetría y no crea un segundo almacén de tokens OAuth:
rclone guarda las credenciales en su propia configuración.

## Lo que el usuario ve hoy

- Dolphin recibe errores de KIO legibles cuando rclone no puede listar, leer, escribir o autenticar.
- Las notificaciones de subida reciben el porcentaje en vivo de rclone, la velocidad, el ETA y el estado de finalización.
- Los builds del paquete guardan el resultado completo de CTest en la terminal o en el log de CI.

Por defecto no se crea ningún log de depuración persistente de KIO Rclone.
Esto es intencional: un log siempre activo podría revelar nombres de remotos y
rutas locales.

## Información segura para un reporte de error

Corré estos comandos y revisá su salida antes de adjuntarla:

~~~bash
rclone version
rclone config redacted
~~~

Para un remoto puntual, un listado detallado de solo lectura también ayuda:

~~~bash
rclone lsd 'Remote:' -vv
~~~

Incluí las versiones de Plasma, KDE Frameworks y rclone; si el remoto usa un
cliente OAuth privado; la acción exacta que falló en Dolphin; el error visible;
y si también se reproduce con `rclone lsd`, `rclone cat` o `rclone rcat`.

Algunos sistemas reenvían los mensajes de KIO/Qt al journal del usuario:

~~~bash
journalctl --user -b --no-pager | rg -i 'kio|rclone'
~~~

Revisá esa salida en privado: puede contener nombres de archivo o rutas.

## Nunca compartas

No publiques `rclone config show`, tokens OAuth, client secrets de Google, un
archivo de configuración sin redactar, rutas privadas, nombres de documentos
ni contenido de archivos que no venga al caso. `rclone config redacted` es un
punto de partida, no una garantía; revisalo antes de publicarlo.
