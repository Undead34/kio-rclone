---
layout: home
title: KIO Rclone — rclone cloud storage in KDE Dolphin
titleTemplate: false
description: KDE Dolphin integration for rclone cloud remotes through KIO.

hero:
  name: KIO Rclone
  text: Your cloud as a Dolphin location
  tagline: Browse, copy, and manage any rclone remote without KDE Online Accounts.
  image:
    src: /kio-rclone.svg
    alt: KIO Rclone
  actions:
    - theme: brand
      text: Get started
      link: /getting-started
    - theme: alt
      text: Configure Google Drive
      link: /google-drive

features:
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"/></svg>'
    title: Native Dolphin integration
    details: Open rclone:/ like any other location. List, copy, paste, create folders, rename, and delete.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></svg>'
    title: Your credentials stay in rclone
    details: No KAccounts or LibKGAPI. rclone handles OAuth, tokens, retries, and providers.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/></svg>'
    title: Useful pause and cancellation
    details: Transfers go through KIO so Dolphin can actually stop the byte stream.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M6 18v-4M12 18V6M18 18v-9"/></svg>'
    title: Visible remote progress
    details: Uploads show percentage, speed, ETA, and rclone's final confirmation phase.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M17.5 19a4.5 4.5 0 0 0 0-9 6 6 0 0 0-11.4-1.5A4.5 4.5 0 0 0 6.5 19Z"/></svg>'
    title: More than Google Drive
    details: Works with the remotes you already configured in rclone.
  - icon: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M21 8 12 3 3 8l9 5 9-5Z"/><path d="M3 8v8l9 5 9-5V8"/><path d="M12 13v8"/></svg>'
    title: Native and simple
    details: A small KIO worker, a Qt configurator, and regular distribution packages.
---

## One source of truth

KIO Rclone does not create another account or duplicate your configuration. It
opens the remote that already works with rclone and presents it to Dolphin as
`rclone:/`.

<ArchitectureFlow />

Start with [Getting started](/getting-started), review the
[features](/features), or configure [Google Drive with your own Google
Cloud project](/google-drive).
