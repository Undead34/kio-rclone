# Transferencias, pausa y progreso

Una copia de Dolphin parece simple, pero entre un archivo local y un proveedor
cloud hay dos flujos que deben mantenerse coordinados:

~~~text
archivo local ⇄ KIO ⇄ KIO Rclone ⇄ rclone ⇄ proveedor
~~~

## Subidas

Al subir, KIO Rclone inicia `rclone rcat` y va entregándole datos solo cuando
rclone puede aceptarlos. Si Dolphin pausa la tarea, KIO deja de alimentar el
worker; el proceso de rclone se queda sin nuevos bytes que enviar.

La notificación puede pasar por estas fases:

| Estado | Significado |
| --- | --- |
| Preparing upload… | rclone está creando la operación y resolviendo el remoto. |
| Uploading… 42% · 8.1 MiB/s | Estadística real que rclone reporta desde el backend. |
| Finalizing upload… | Todos los bytes locales llegaron a rclone; el proveedor aún confirma el último bloque. |
| Completado | rclone salió correctamente y KIO confirmó la tarea. |

> [!IMPORTANT]
> La barra principal de Dolphin puede alcanzar 100% antes de terminar
> `Finalizing upload…`. Es una limitación del contador de `FileCopyJob` de KIO,
> no una copia terminada a medias. Espera a que la notificación desaparezca con
> éxito.

## Descargas

La descarga se entrega por chunks a KIO. Pausar bloquea el consumidor y aplica
contrapresión al proceso que está leyendo desde rclone. Cancelar termina el
proceso y la tarea de KIO.

## Rendimiento

Tres cosas influyen más que el worker:

1. El Client ID OAuth del proveedor. En Google Drive, usa uno propio.
2. La cuota, latencia y límites del backend remoto.
3. El tamaño de chunk configurado para el backend. Google Drive usa 8 MiB por
   defecto; reducirlo ahorra memoria, pero puede bajar el rendimiento.

Para una medición honesta, compara el mismo archivo, red, remoto y Client ID.
No compares una transferencia bajo una cuota compartida con otra bajo un
proyecto OAuth privado.

## Qué probar si algo parece raro

- Pausa una descarga grande: la actividad de red debe estabilizarse.
- Reanuda: la copia debe continuar y terminar sin duplicar el archivo.
- Sube un archivo de varios chunks: observa porcentaje, velocidad y
  `Finalizing upload…`.
- Cancela: el archivo remoto no debería figurar como una copia terminada.
- Pulsa F5 al terminar: Dolphin debe pedir de nuevo el listado remoto.

Si falla, sigue la guía de [diagnóstico seguro](/LOGGING).
