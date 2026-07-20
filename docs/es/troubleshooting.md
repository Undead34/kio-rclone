---
description: Soluciones para transferencias lentas de Google Drive, errores de OAuth y rclone:/ que no aparece en Dolphin.
---

# Problemas frecuentes

## Google Drive tarda mucho antes de mover bytes

Casi siempre es cuota o OAuth, no tu Wi-Fi. El Client ID compartido de rclone
es usado por muchas personas y Google puede limitar consultas antes de que la
transferencia empiece.

**Solución:** configura un Client ID propio en
[Google Drive y GCP](/es/google-drive), autoriza de nuevo el remoto y vuelve a
probar.

## “Access denied”, “invalid_grant” o el remoto pide login

La autorización expiró, se revocó o el Client ID cambió.

1. Abre `kio-rclone-config`.
2. Selecciona el remoto.
3. Pulsa **Reconnect…**.
4. Si es Google Drive, comprueba que usas el mismo Client ID/secret con el que
   quieres renovar el token.

Si el proyecto Google sigue en modo **Testing**, revisa el vencimiento de siete
días en la guía [GCP](/es/google-drive#modo-testing-y-tokens-de-siete-dias).

## La barra dice 100% pero la tarea aún sigue

Espera los textos **Finalizing upload…** y **Committing upload…**. Drive u otro
proveedor todavía puede estar confirmando el último bloque o publicando el
nombre definitivo. No cierres ni canceles la tarea si quieres que termine
correctamente.

## LibreOffice abre un documento vacío o dice que está corrupto

Las exportaciones nativas de Google y los nombres duplicados se abren de solo
lectura tras materializarse localmente: exportaciones cuyo tamaño aparece como
desconocido y objetos distintos que comparten el mismo nombre.

Si Dolphin indica que hay duplicados, KIO Rclone abre exactamente el objeto
más reciente cuando Drive proporciona su ID, pero mantiene la ruta de solo
lectura. Renombra o elimina los duplicados desde Drive/rclone antes de editar.

Los documentos nativos de Google exportados como DOCX/XLSX/PPTX también se
abren de solo lectura. Guarda una copia con otro nombre si quieres convertirlos
en archivos Office ordinarios; la reimportación automática podría reemplazar
el documento colaborativo original.

## Un TXT u otro archivo ordinario no conserva los cambios

El guardado se publica desde un nombre remoto temporal. Una cancelación, fallo
de red o nueva modificación durante el flush conserva la versión remota
completa en vez de dejar bytes parciales.

Si aún falla, comprueba que el backend soporte `rcat` y `moveto`, que no haya
otro objeto con el mismo nombre y que ninguna otra aplicación haya modificado
el archivo durante la subida.

## No aparece `rclone:/`

1. Confirma que el paquete está instalado: `pacman -Q kio-rclone`.
2. Reconstruye la caché de servicios:
   ~~~bash
   kbuildsycoca6 --noincremental
   ~~~
3. Cierra y vuelve a abrir Dolphin.
4. Confirma que rclone existe:
   ~~~bash
   rclone version
   ~~~

## Falla solo en una empresa o cuenta de Google Workspace

El administrador puede limitar apps OAuth externas, scopes de Drive o el uso de
apps internas. Consulta la sección de Workspace en
[Google Drive y GCP](/es/google-drive#google-workspace-y-cuentas-administradas).

## Cómo pedir ayuda sin filtrar secretos

Sigue [Logs y diagnóstico seguro](/es/logging). Comparte versión, configuración
redactada, pasos exactos y el error visible; nunca tokens ni Client Secret.

---

<sub>Las exportaciones de Google en solo lectura, el manejo de nombres
duplicados y el guardado atómico anteriores llegaron en 0.3.0; las versiones
más antiguas no tienen soporte. Consulta el
[changelog](https://github.com/Undead34/kio-rclone/blob/main/CHANGELOG.md).</sub>
