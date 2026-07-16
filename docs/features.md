# Funciones

KIO Rclone intenta que el remoto se sienta natural en Dolphin, mientras deja
las decisiones específicas del proveedor a rclone.

## Lo que puedes hacer

| Acción en Dolphin | Comportamiento |
| --- | --- |
| Abrir `rclone:/` | Muestra los remotos configurados por rclone. |
| Entrar en carpetas | Consulta el listado del proveedor a través de rclone. |
| Descargar | Lee un solo archivo remoto y lo entrega como stream a KIO. |
| Subir | Envía el archivo a `rclone rcat` sin crear una copia temporal propia. |
| Crear carpeta | Usa `rclone mkdir`. |
| Renombrar/mover dentro del remoto | Usa `rclone moveto`. |
| Borrar | Usa `deletefile`, `rmdir` o `purge` según la operación de KIO. |
| Ver espacio libre | Usa `rclone about` si el backend lo soporta. |
| Configurar | Abre el configurador pequeño de KIO Rclone. |

## Lo que hace especial a las transferencias

Las operaciones entre ubicaciones se transmiten por KIO en vez de delegarse en
una copia directa de rclone. Esto conserva los controles que esperas de
Dolphin:

- Pausar deja de solicitar datos al origen y de alimentar a rclone.
- Cancelar termina el proceso de transferencia.
- Las subidas reciben estadísticas JSON de rclone: porcentaje remoto, velocidad
  y ETA.
- El estado **Finalizing upload…** evita confundir una barra al 100% con una
  subida ya confirmada por el proveedor.

Consulta los detalles en [Transferencias](/transfers).

## Lo que no pretende ser

KIO Rclone no reemplaza otras herramientas de rclone:

| Necesidad | Usa |
| --- | --- |
| Sincronizar directorios | `rclone sync` o `rclone bisync` |
| Montar un remoto como filesystem | `rclone mount` |
| Cola/reintentos fuera de Dolphin | Un flujo propio de rclone |
| Caché offline/VFS | `rclone mount` con las opciones VFS adecuadas |
| Funciones exclusivas del proveedor | El comando/backend de rclone correspondiente |

Esta separación es deliberada: el worker sigue pequeño y rclone mantiene el
conocimiento de proveedores, OAuth y reintentos.
