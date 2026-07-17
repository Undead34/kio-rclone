import { defineConfig } from 'vitepress';

const base = process.env.DOCS_BASE || '/';

const siteUrl = 'https://undead34.github.io/kio-rclone/';
const siteDescription = 'Your cloud as a native Dolphin location.';

export default defineConfig({
    base,
    lang: 'en',
    title: 'KIO Rclone',
    description: siteDescription,
    lastUpdated: true,

    sitemap: {
        hostname: siteUrl,
    },

    locales: {
        root: {
            label: 'English',
            lang: 'en',
        },
        es: {
            label: 'Español',
            lang: 'es',
            link: '/es/',
            themeConfig: {
                localeLinks: { text: 'Idioma' },
                nav: [
                    { text: 'Empezar', link: '/es/getting-started' },
                    { text: 'Google Drive', link: '/es/google-drive' },
                    { text: 'Funciones', link: '/es/features' },
                    { text: 'Ayuda', link: '/es/troubleshooting' },
                ],
                sidebar: [
                    {
                        text: 'Para usuarios',
                        items: [
                            { text: 'Primeros pasos', link: '/es/getting-started' },
                            { text: 'Funciones', link: '/es/features' },
                            { text: 'Transferencias', link: '/es/transfers' },
                            { text: 'Google Drive y GCP', link: '/es/google-drive' },
                            { text: 'Problemas frecuentes', link: '/es/troubleshooting' },
                        ],
                    },
                    {
                        text: 'Soporte',
                        items: [{ text: 'Logs y diagnóstico seguro', link: '/es/logging' }],
                    },
                ],
                outline: {
                    level: [2, 3],
                    label: 'En esta página',
                },
                editLink: {
                    pattern: 'https://github.com/Undead34/kio-rclone/edit/main/docs/:path',
                    text: 'Editar esta página en GitHub',
                },
                docFooter: {
                    prev: 'Anterior',
                    next: 'Siguiente',
                },
                lastUpdated: {
                    text: 'Actualizado',
                    formatOptions: {
                        dateStyle: 'long',
                        forceLocale: true,
                    },
                },
                darkModeSwitchLabel: 'Apariencia',
                lightModeSwitchTitle: 'Cambiar al tema claro',
                darkModeSwitchTitle: 'Cambiar al tema oscuro',
                sidebarMenuLabel: 'Menú',
                returnToTopLabel: 'Volver arriba',
                skipToContentLabel: 'Saltar al contenido',
                footer: {
                    message: 'GPL-2.0-or-later · Sin telemetría · Las credenciales siguen en rclone',
                    copyright: 'KIO Rclone contributors',
                },
            },
        },
    },

    head: [
        ['link', { rel: 'icon', type: 'image/svg+xml', href: '/kio-rclone.svg' }],
        ['meta', { name: 'theme-color', content: '#1688d4' }],
        ['meta', { property: 'og:type', content: 'website' }],
        ['meta', { property: 'og:site_name', content: 'KIO Rclone' }],
        ['meta', { property: 'og:title', content: 'KIO Rclone' }],
        ['meta', { property: 'og:description', content: siteDescription }],
        ['meta', { property: 'og:url', content: siteUrl }],
        ['meta', { property: 'og:image', content: `${siteUrl}og-image.png` }],
        ['meta', { name: 'twitter:card', content: 'summary' }],
        ['meta', { name: 'twitter:title', content: 'KIO Rclone' }],
        ['meta', { name: 'twitter:description', content: siteDescription }],
        ['meta', { name: 'twitter:image', content: `${siteUrl}og-image.png` }],
    ],

    themeConfig: {
        logo: '/kio-rclone.svg',
        localeLinks: { text: 'Language' },

        nav: [
            { text: 'Get started', link: '/getting-started' },
            { text: 'Google Drive', link: '/google-drive' },
            { text: 'Features', link: '/features' },
            { text: 'Help', link: '/troubleshooting' },
        ],

        sidebar: [
            {
                text: 'For users',
                items: [
                    { text: 'Getting started', link: '/getting-started' },
                    { text: 'Features', link: '/features' },
                    { text: 'Transfers', link: '/transfers' },
                    { text: 'Google Drive and GCP', link: '/google-drive' },
                    { text: 'Troubleshooting', link: '/troubleshooting' },
                ],
            },
            {
                text: 'Support',
                items: [{ text: 'Logs and safe diagnostics', link: '/logging' }],
            },
            {
                text: 'Project',
                items: [
                    { text: 'Testing', link: '/testing' },
                    { text: 'Distribution and releases', link: '/distribution-plan' },
                ],
            },
        ],

        outline: {
            level: [2, 3],
            label: 'On this page',
        },

        socialLinks: [
            {
                icon: 'github',
                link: 'https://github.com/Undead34/kio-rclone',
                ariaLabel: 'KIO Rclone repository on GitHub',
            },
        ],

        editLink: {
            pattern: 'https://github.com/Undead34/kio-rclone/edit/main/docs/:path',
            text: 'Edit this page on GitHub',
        },

        search: {
            provider: 'local',
            options: {
                locales: {
                    es: {
                        translations: {
                            button: {
                                buttonText: 'Buscar',
                                buttonAriaLabel: 'Buscar',
                            },
                            modal: {
                                displayDetails: 'Mostrar detalles',
                                resetButtonTitle: 'Restablecer búsqueda',
                                backButtonTitle: 'Cerrar búsqueda',
                                noResultsText: 'No hay resultados',
                                footer: {
                                    selectText: 'Seleccionar',
                                    selectKeyAriaLabel: 'Intro',
                                    navigateText: 'Navegar',
                                    navigateUpKeyAriaLabel: 'Flecha arriba',
                                    navigateDownKeyAriaLabel: 'Flecha abajo',
                                    closeText: 'Cerrar',
                                    closeKeyAriaLabel: 'Esc',
                                },
                            },
                        },
                    },
                },
            },
        },

        docFooter: {
            prev: 'Previous',
            next: 'Next',
        },

        lastUpdated: {
            text: 'Updated',
            formatOptions: {
                dateStyle: 'long',
                forceLocale: true,
            },
        },

        darkModeSwitchLabel: 'Appearance',
        lightModeSwitchTitle: 'Switch to light theme',
        darkModeSwitchTitle: 'Switch to dark theme',
        sidebarMenuLabel: 'Menu',
        returnToTopLabel: 'Back to top',
        skipToContentLabel: 'Skip to content',

        footer: {
            message: 'GPL-2.0-or-later · No telemetry · Credentials stay in rclone',
            copyright: 'KIO Rclone contributors',
        },
    },
});
