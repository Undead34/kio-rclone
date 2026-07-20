---
description: Crea tu propio cliente OAuth de Google Cloud para que Google Drive no dependa de la cuota compartida de rclone.
---

# Google Drive y tu proyecto de Google Cloud

Usar un Client ID propio es la forma recomendada de conectar Google Drive con
KIO Rclone. Evita la cuota compartida del Client ID público de rclone y hace
que tu límite de API dependa de tu proyecto, no de los demás usuarios.

Esta guía crea un cliente **personal** para usar con tu propia cuenta y tus
propios remotos. No es una receta para publicar una aplicación OAuth para
terceros.

> [!TIP]
> rclone también mantiene una [guía oficial para crear tu propio Client ID](https://rclone.org/drive/#making-your-own-client-id).
> Si Google cambia el nombre de un botón en la consola, sigue esa guía junto a
> esta página: los conceptos y los valores son los mismos.

## Antes de empezar

Necesitas:

- una cuenta de Google;
- acceso a [Google Cloud Console](https://console.cloud.google.com/);
- KIO Rclone instalado;
- rclone funcionando en tu sesión.

No necesitas un servidor web, una URL de callback propia ni una cuenta de
servicio. Para este caso usa un **OAuth client de tipo Desktop app**.

## Qué permiso pide KIO Rclone

El remoto creado por KIO Rclone solicita el scope de Drive completo:

```text
https://www.googleapis.com/auth/drive
```

Es el scope que permite listar, descargar, subir, crear carpetas, renombrar y
borrar los archivos a los que tu cuenta ya tiene acceso. No crees un proyecto
con ese Client ID para otras personas si no entiendes las obligaciones de
OAuth/verification de Google.

## Paso 1: crea un proyecto

1. Abre [Crear proyecto](https://console.cloud.google.com/projectcreate).
2. Pulsa **New Project**.
3. Dale un nombre claro, por ejemplo `KIO Rclone personal`.
4. Selecciona el proyecto recién creado en la barra superior de la consola.

Un proyecto separado para este uso evita mezclar sus cuotas y configuración
OAuth con aplicaciones ajenas.

## Paso 2: habilita la Google Drive API

1. Abre la [biblioteca de APIs](https://console.cloud.google.com/apis/library/drive.googleapis.com).
2. Comprueba que el proyecto correcto está seleccionado.
3. Pulsa **Enable** en **Google Drive API**.

Google exige habilitar el producto API antes de que el cliente OAuth pueda
pedir sus scopes. La guía oficial de Google también cubre
[proyectos y credenciales](https://developers.google.com/workspace/guides/create-project).

## Paso 3: configura Google Auth Platform

En la consola abre **Google Auth Platform**. La interfaz de Google puede
mostrar estos pasos como **Branding**, **Audience** y **Data Access**.

### Branding

Completa como mínimo:

- nombre de la app, por ejemplo `KIO Rclone personal`;
- correo de soporte;
- correo de contacto del desarrollador.

Para un cliente personal no necesitas inventar una página web pública. Usa
datos reales que puedas reconocer si Google muestra la pantalla de
consentimiento.

### Audience

Elige según tu cuenta:

| Tipo de cuenta | Opción habitual |
| --- | --- |
| Gmail, Google personal o varias cuentas externas | **External** |
| Solo usuarios de una organización Google Workspace propia | **Internal**, si tu administrador lo permite |

Con **External** la app empieza normalmente en **Testing**. Añade tu propia
dirección de Google en la lista de test users antes de autorizar.

### Data Access

Añade el scope de Drive completo si la consola te pide declarar scopes:

```text
https://www.googleapis.com/auth/drive
```

La pantalla de consentimiento debe reflejar que la aplicación tendrá acceso
a Drive. No añadas Gmail, Calendar u otros scopes: KIO Rclone no los necesita.

## Paso 4: crea las credenciales OAuth

1. Abre **Google Auth Platform → Clients**.
2. Pulsa **Create client**.
3. Elige **Desktop app** como tipo de aplicación.
4. Ponle un nombre, por ejemplo `KIO Rclone en mi PC`.
5. Crea el cliente.
6. Copia el **Client ID** y el **Client secret** inmediatamente, o descarga el
   JSON de las credenciales.

El token que rclone guarda después es lo que concede acceso a tu Drive. No
subas el JSON ni el archivo `rclone.conf` a Git, y no pegues credenciales o
tokens en tickets ni chats.
[La ayuda oficial de clientes OAuth](https://support.google.com/cloud/answer/15549257)
describe el flujo actual de la consola.

## Paso 5: conéctalo con KIO Rclone

1. Ejecuta:

   ```bash
   kio-rclone-config
   ```

2. Selecciona tu remoto de Google Drive.
3. Pulsa **Google OAuth…**.
4. Pega el Client ID y el Client secret que acabas de crear.
5. Acepta y completa el navegador que abre rclone.
6. Vuelve a Dolphin y entra en `rclone:/`.

Si esta máquina ya tiene un override OAuth de KDE, el diálogo puede ofrecer
**Import existing local OAuth override**. Es una migración de credenciales:
KIO Rclone sigue usando rclone para guardar y refrescar el token.

## Paso 6: verifica sin revelar nada

Prueba una operación de lectura:

```bash
rclone lsd 'Google Drive:'
```

Para revisar tu configuración sin publicar tokens:

```bash
rclone config redacted
```

Después abre `rclone:/Google%20Drive/` en Dolphin y prueba crear una carpeta
dentro de un directorio de pruebas.

## Modo Testing y tokens de siete días

Este punto es importante. Google limita las apps **External** que siguen en
**Testing** a los test users que agregaste. Además, cuando una app pide scopes
de Drive, la autorización y el refresh token de un test user expiran después
de siete días.

Si cada semana te vuelve a pedir login, revisa primero que el proyecto sigue
en Testing; no es un bug de KIO Rclone. Google documenta este comportamiento en
[Manage App Audience](https://support.google.com/cloud/answer/15549945).

Para un Client ID estrictamente personal, rclone explica la opción de publicar
tu app sin verificar y aceptar el aviso de app no verificada, con el fin de
evitar ese vencimiento semanal. Hazlo solo si entiendes la pantalla de Google,
es para tus propias cuentas y no estás distribuyendo ese Client ID a terceros.
Para una app pública, múltiples usuarios o scopes restringidos, revisa los
requisitos actuales de verificación de Google antes de pasar a producción.

## Google Workspace y cuentas administradas

En una empresa o centro educativo puede fallar aunque el Client ID sea válido:

- **Internal** solo permite usuarios de la organización correspondiente.
- El administrador puede bloquear apps OAuth externas.
- La política de API puede limitar el scope de Drive.
- Puede hacer falta aprobar el Client ID en Google Admin.

En esos casos, comparte con el administrador el Client ID y el scope exacto
que solicita la aplicación; no compartas tokens ni tu Client secret.

## Errores comunes

| Error | Causa probable | Qué hacer |
| --- | --- | --- |
| `access_denied` | Tu cuenta no está en test users o una política bloquea la app. | Añade la cuenta en Audience o consulta al admin Workspace. |
| `org_internal` | Intentaste usar un cliente Internal desde fuera de la organización. | Usa una cuenta de la organización o crea un cliente External personal. |
| `invalid_client` | Client ID/secret copiado mal o de otro proyecto. | Vuelve a copiar las credenciales y reconecta el remoto. |
| Login cada siete días | App External sigue en Testing con scope de Drive. | Lee la sección anterior y decide la configuración de publicación adecuada. |
| Listado lento / `RATE_LIMIT_EXCEEDED` | Client ID compartido o cuota del proyecto. | Usa tu Client ID y revisa la cuota/API en Cloud Console. |

## Seguridad mínima

- Crea un proyecto exclusivo para este uso.
- No subas el JSON de credenciales a Git.
- No compartas Client secret, refresh token ni `rclone.conf` sin redactar.
- Usa `rclone config redacted` para pedir ayuda.
- Si pierdes el control del equipo, revoca el acceso desde tu cuenta de Google
  y vuelve a autorizar el remoto.

¿Todo listo? Vuelve a [Primeros pasos](/es/getting-started) o consulta
[Problemas frecuentes](/es/troubleshooting).
