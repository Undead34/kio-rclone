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
      link: /es/getting-started
    - theme: alt
      text: Configurar Google Drive
      link: /es/google-drive

features:
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"/></svg>'
    title: Integración real con Dolphin
    details: Abre rclone:/ como una ubicación más. Listado, copiar, pegar, crear carpetas, renombrar y borrar.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></svg>'
    title: Tus credenciales, en rclone
    details: No hay KAccounts ni LibKGAPI. rclone gestiona OAuth, tokens, reintentos y proveedores.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/></svg>'
    title: Pausa y cancelación útiles
    details: Las transferencias pasan por KIO para que Dolphin pueda frenar el flujo de bytes, no solo cambiar una etiqueta.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M6 18v-4M12 18V6M18 18v-9"/></svg>'
    title: Progreso remoto visible
    details: Las subidas muestran porcentaje, velocidad, ETA y la fase de confirmación final de rclone.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M17.5 19a4.5 4.5 0 0 0 0-9 6 6 0 0 0-11.4-1.5A4.5 4.5 0 0 0 6.5 19Z"/></svg>'
    title: Más que Google Drive
    details: Funciona con los remotos que ya tengas configurados en rclone, sin reimplementar cada API.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M21 8 12 3 3 8l9 5 9-5Z"/><path d="M3 8v8l9 5 9-5V8"/><path d="M12 13v8"/></svg>'
    title: Nativo y simple
    details: Un worker KIO pequeño, un configurador Qt y paquetes normales de tu distribución.

---

## Una sola fuente de verdad

KIO Rclone no crea otra cuenta ni otra copia de tu configuración. Abre el
remoto que ya funciona con rclone y lo presenta a Dolphin mediante `rclone:/`.

<ArchitectureFlow lang="es" />

Empieza en [Primeros pasos](/es/getting-started), revisa las
[funciones](/es/features) o configura [Google Drive con tu propio proyecto de
Google Cloud](/es/google-drive).
