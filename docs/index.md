---
layout: home
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
      link: /en/getting-started
    - theme: alt
      text: Configure Google Drive
      link: /en/google-drive

features:
  - icon: 🐬
    title: Native Dolphin integration
    details: Open rclone:/ like any other location. List, copy, paste, create folders, rename, and delete.
  - icon: 🔐
    title: Your credentials stay in rclone
    details: No KAccounts or LibKGAPI. rclone handles OAuth, tokens, retries, and providers.
  - icon: ⏯️
    title: Useful pause and cancellation
    details: Transfers go through KIO so Dolphin can actually stop the byte stream.
  - icon: ⚡
    title: Visible remote progress
    details: Uploads show percentage, speed, ETA, and rclone's final confirmation phase.
  - icon: ☁️
    title: More than Google Drive
    details: Works with the remotes you already configured in rclone.
  - icon: 🧩
    title: Native and simple
    details: A small KIO worker, a Qt configurator, and regular distribution packages.
---

## One source of truth

KIO Rclone does not create another account or duplicate your configuration. It
opens the remote that already works with rclone and presents it to Dolphin as
`rclone:/`.

~~~text
Dolphin → KIO Rclone → rclone → your provider
~~~

Start with [Getting started](/en/getting-started), review the
[features](/en/features), or configure [Google Drive with your own Google
Cloud project](/en/google-drive).
