---
description: Qué puede hacer Dolphin a través de rclone:/ y qué no intenta reemplazar KIO Rclone.
---

# Funciones

KIO Rclone intenta que el remoto se sienta natural en Dolphin, mientras deja
las decisiones específicas del proveedor a rclone.

## Lo que puedes hacer

| Acción en Dolphin | Comportamiento |
| --- | --- |
| Abrir `rclone:/` | Muestra los remotos configurados por rclone. |
| Entrar en carpetas | Consulta el listado del proveedor a través de rclone. |
| Descargar | Transmite archivos normales; materializa primero los de tamaño desconocido o nombre duplicado. |
| Abrir en LibreOffice o un editor | Usa la caché de archivo completo de KIOFuse para dar acceso local y seekable. |
| Subir/guardar | Sube a un nombre remoto temporal y publica el resultado sólo cuando la transferencia termina. |
| Crear carpeta | Usa `rclone mkdir`. |
| Renombrar/mover dentro del remoto | Usa `rclone moveto`. |
| Borrar | Usa `deletefile`, `rmdir` o `purge` según la operación de KIO. |
| Ver espacio libre | Usa `rclone about` si el backend lo soporta. |
| Configurar | Abre el configurador pequeño de KIO Rclone. |

## Reapertura rápida, con frescura acotada

KIO Rclone guarda una caché privada y pequeña de listados completos que
terminaron correctamente en la ubicación de caché de KDE (normalmente
`~/.cache/kio-rclone/`). Usa la caché compartida de KDE, así que puede
reutilizarse incluso después de cerrar Dolphin y su proceso worker. Es una
optimización breve de navegación, no un sistema de archivos sin conexión.

La política predeterminada **Caché fresca** reutiliza un listado durante 15
segundos. Así una carpeta visitada justo antes de cerrar Dolphin puede abrirse
sin otra ida y vuelta al proveedor. El configurador permite elegir de 1 a 60
segundos, o seleccionar **Estricto** para preguntar siempre a rclone y al
proveedor.

- Sólo se guardan listados completos y correctos; un error o una cancelación
  nunca se persiste.
- La caché está limitada a 8 MiB, expulsa los snapshots menos usados, no
  contiene credenciales de rclone y se invalida automáticamente cuando cambia
  la configuración de rclone.
- Las descargas y toda operación que modifica datos siguen resolviendo el
  objetivo contra el remoto. Una subida, creación, renombre o eliminación
  exitosa limpia los snapshots y notifica el cambio a las vistas KIO abiertas.
- Un cliente KIO puede pedir `cache=reload` o `cache=refresh` para descartar
  snapshots y forzar un listado remoto. En un listado fresco normal puede haber
  datos modificados por otro cliente durante la ventana seleccionada; usa
  **Estricto** cuando eso no sea aceptable.

No hay recorrido de árbol en segundo plano, `ListR` ni una corrección visual
cache-first en esta ruta. Añadirlos introduciría actividad de red y cambios
sorpresivos en la vista sin resolver tan directamente la demora habitual de
cerrar y reabrir Dolphin.

## Google Drive es un hub pequeño, no una raíz plana

Para un remoto de Google Drive, Dolphin organiza su raíz en **Mi unidad**,
**Compartido conmigo**, **Unidades compartidas**, **Papelera** y **Favoritos**.
Así el árbol principal es predecible y cada consulta específica del proveedor
sigue delegada a rclone. Mi unidad y una unidad compartida seleccionada pueden
modificarse si el proveedor lo permite; las vistas filtradas son
deliberadamente de solo lectura. También puedes abrir una carpeta de Drive de
ID conocido desde la barra de ubicación sin crear otra base de cuentas.

Consulta [Google Drive y GCP](/es/google-drive#vistas-de-drive-en-dolphin)
para ver las rutas exactas y los límites conservadores intencionales.

## Lo que hace especial a las transferencias

Las operaciones entre ubicaciones se transmiten por KIO en vez de delegarse en
una copia directa de rclone. Esto conserva los controles que esperas de
Dolphin:

- Pausar deja de solicitar datos al origen y de alimentar a rclone.
- Cancelar termina el proceso de transferencia.
- Una subida cancelada o fallida no sustituye el archivo remoto por una copia
  parcial.
- Las subidas reciben estadísticas JSON de rclone: porcentaje remoto, velocidad
  y ETA.
- El estado **Finalizing upload…** evita confundir una barra al 100% con una
  subida ya confirmada por el proveedor.

Consulta los detalles en [Transferencias](/es/transfers).

<!--
## Documentos, edición y Google Drive

Los archivos ordinarios como TXT, ODT, DOCX, XLSX o PPTX se abren mediante la
caché local de KIOFuse. Al guardar, KIO Rclone valida el archivo completo y
publica la nueva versión al final.

Hay dos excepciones de solo lectura:

- Los documentos nativos de Google exportados por rclone no tienen un tamaño
  estable hasta descargarlos. Se abren con el tamaño correcto, pero no se
  reimportan automáticamente para evitar sustituir el documento colaborativo.
- Si una carpeta contiene varios objetos con el mismo nombre, se muestra el
  más reciente y se descarga por su ID cuando el backend lo permite. La ruta
  queda bloqueada para edición hasta resolver los duplicados.
-->

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
