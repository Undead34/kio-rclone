# Problemas frecuentes

## Google Drive tarda mucho antes de mover bytes

Casi siempre es cuota o OAuth, no tu Wi-Fi. El Client ID compartido de rclone
es usado por muchas personas y Google puede limitar consultas antes de que la
transferencia empiece.

**Solución:** configura un Client ID propio en
[Google Drive y GCP](/google-drive), autoriza de nuevo el remoto y vuelve a
probar.

## “Access denied”, “invalid_grant” o el remoto pide login

La autorización expiró, se revocó o el Client ID cambió.

1. Abre `kio-rclone-config`.
2. Selecciona el remoto.
3. Pulsa **Reconnect…**.
4. Si es Google Drive, comprueba que usas el mismo Client ID/secret con el que
   quieres renovar el token.

Si el proyecto Google sigue en modo **Testing**, revisa el vencimiento de siete
días en la guía [GCP](/google-drive#modo-testing-y-tokens-de-siete-dias).

## La barra dice 100% pero la tarea aún sigue

Espera el texto **Finalizing upload…**. Drive u otro proveedor todavía puede
estar confirmando el último bloque remoto. No cierres ni canceles la tarea si
quieres que termine correctamente.

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
[Google Drive y GCP](/google-drive#google-workspace-y-cuentas-administradas).

## Cómo pedir ayuda sin filtrar secretos

Sigue [Logs y diagnóstico seguro](/LOGGING). Comparte versión, configuración
redactada, pasos exactos y el error visible; nunca tokens ni Client Secret.
