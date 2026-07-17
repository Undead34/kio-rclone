---
layout: home
description: Integración de KDE Dolphin con remotos cloud de rclone mediante KIO.

hero:
  name: KIO Rclone
  text: Tu nube como una ubicación de Dolphin
  tagline: Navega, copia y gestiona cualquier remoto de rclone sin KDE Online Accounts.
  image:
    src: /kio-rclone.svg
    alt: KIO Rclone
  actions:
    - theme: brand
      text: Empezar
      link: /getting-started
    - theme: alt
      text: Configurar Google Drive
      link: /google-drive

features:
  - icon: 🐬
    title: Integración real con Dolphin
    details: Abre rclone:/ como una ubicación más. Listado, copiar, pegar, crear carpetas, renombrar y borrar.
  - icon: 🔐
    title: Tus credenciales, en rclone
    details: No hay KAccounts ni LibKGAPI. rclone gestiona OAuth, tokens, reintentos y proveedores.
  - icon: ⏯️
    title: Pausa y cancelación útiles
    details: Las transferencias pasan por KIO para que Dolphin pueda frenar el flujo de bytes, no solo cambiar una etiqueta.
  - icon: ⚡
    title: Progreso remoto visible
    details: Las subidas muestran porcentaje, velocidad, ETA y la fase de confirmación final de rclone.
  - icon: ☁️
    title: Más que Google Drive
    details: Funciona con los remotos que ya tengas configurados en rclone, sin reimplementar cada API.
  - icon: 🧩
    title: Nativo y simple
    details: Un worker KIO pequeño, un configurador Qt y paquetes normales de tu distribución.

---

## Una sola fuente de verdad

KIO Rclone no crea otra cuenta ni otra copia de tu configuración. Abre el
remoto que ya funciona con rclone y lo presenta a Dolphin mediante `rclone:/`.

~~~text
Dolphin → KIO Rclone → rclone → tu proveedor
~~~

Empieza en [Primeros pasos](/getting-started), revisa las
[funciones](/features) o configura [Google Drive con tu propio proyecto de
Google Cloud](/google-drive).
