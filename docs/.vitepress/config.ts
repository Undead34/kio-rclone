import { defineConfig } from 'vitepress';

const base = process.env.DOCS_BASE || '/';

export default defineConfig({
    base,
    lang: 'es',
    title: 'KIO Rclone',
    description: 'Tu nube como una ubicación nativa de Dolphin.',
    lastUpdated: true,

    locales: {
        root: { label: 'Español', lang: 'es' },
        en: { label: 'English', lang: 'en' },
    },

    head: [
        ['meta', { name: 'theme-color', content: '#1688d4' }],
        ['meta', { property: 'og:type', content: 'website' }],
        ['meta', { property: 'og:site_name', content: 'KIO Rclone' }],
        ['meta', { property: 'og:title', content: 'KIO Rclone' }],
        ['meta', { property: 'og:description', content: 'Tu nube como una ubicación nativa de Dolphin.' }],
    ],

    themeConfig: {
        logo: '/kio-rclone.svg',
        localeLinks: { text: 'Idioma' },

        locales: {
            root: {
                label: 'Español',
                lang: 'es',
            },
            en: {
                label: 'English',
                lang: 'en',
                nav: [
                    { text: 'Get started', link: '/en/getting-started' },
                    { text: 'Google Drive', link: '/en/google-drive' },
                    { text: 'Features', link: '/en/features' },
                    { text: 'Help', link: '/en/troubleshooting' },
                ],
                sidebar: [
                    {
                        text: 'For users',
                        items: [
                            { text: 'Home', link: '/en/' },
                            { text: 'Getting started', link: '/en/getting-started' },
                            { text: 'Features', link: '/en/features' },
                            { text: 'Transfers', link: '/en/transfers' },
                            { text: 'Google Drive and GCP', link: '/en/google-drive' },
                            { text: 'Troubleshooting', link: '/en/troubleshooting' },
                        ],
                    },
                    {
                        text: 'Support',
                        items: [{ text: 'Logs and safe diagnostics', link: '/en/LOGGING' }],
                    },
                ],
            },
        },

        nav: [
            { text: 'Empezar', link: '/getting-started' },
            { text: 'Google Drive', link: '/google-drive' },
            { text: 'Funciones', link: '/features' },
            { text: 'Ayuda', link: '/troubleshooting' },
        ],

        sidebar: [
            {
                text: 'Para usuarios',
                items: [
                    { text: 'Inicio', link: '/' },
                    { text: 'Primeros pasos', link: '/getting-started' },
                    { text: 'Funciones', link: '/features' },
                    { text: 'Transferencias', link: '/transfers' },
                    { text: 'Google Drive y GCP', link: '/google-drive' },
                    { text: 'Problemas frecuentes', link: '/troubleshooting' },
                ],
            },
            {
                text: 'Soporte',
                items: [
                    { text: 'Logs y diagnóstico seguro', link: '/LOGGING' },
                ],
            },
            {
                text: 'Proyecto',
                items: [
                    { text: 'Pruebas', link: '/TESTING' },
                    { text: 'Distribución y releases', link: '/DISTRIBUTION_PLAN' },
                ],
            },
        ],

        outline: {
            level: [2, 3],
            label: 'En esta página',
        },

        socialLinks: [
            {
                icon: 'github',
                link: 'https://github.com/Undead34/kio-rclone',
                ariaLabel: 'Repositorio de KIO Rclone en GitHub',
            },
        ],

        editLink: {
            pattern: 'https://github.com/Undead34/kio-rclone/edit/main/docs/:path',
            text: 'Editar esta página en GitHub',
        },

        search: {
            provider: 'local',
            options: {
                locales: {
                    root: {
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
});
