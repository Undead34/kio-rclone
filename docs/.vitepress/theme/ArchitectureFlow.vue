<!--
SPDX-FileCopyrightText: 2026 KIO Rclone contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->
<script setup lang="ts">
interface Node {
    id: string;
    title: string;
    desc: string;
    icon: string;
    highlight?: boolean;
}

const props = withDefaults(defineProps<{ lang?: 'en' | 'es'; compact?: boolean }>(), {
    lang: 'en',
    compact: false,
});

// Icons are language-independent; only title/desc change per locale. The
// "kio-rclone" node uses the real project logo instead of a generic glyph.
const icons: Record<string, string> = {
    local: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"/>',
    kio: '<rect x="3" y="4" width="18" height="6" rx="1.5"/><rect x="3" y="14" width="18" height="6" rx="1.5"/>',
    'kio-rclone':
        '<rect x="3" y="11" width="7" height="7" rx="1"/><rect x="14" y="11" width="7" height="7" rx="1"/><rect x="8.5" y="3" width="7" height="7" rx="1"/><path d="M12 10v1M6.5 11v-2a2 2 0 0 1 2-2h5a2 2 0 0 1 2 2v2"/>',
    rclone: '<polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/>',
    provider: '<path d="M17.5 19a4.5 4.5 0 0 0 0-9 6 6 0 0 0-11.4-1.5A4.5 4.5 0 0 0 6.5 19Z"/>',
};

const logoSvg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="af-cloud" x1="48" y1="42" x2="209" y2="215" gradientUnits="userSpaceOnUse">
      <stop stop-color="#6ad7ff"/><stop offset="0.52" stop-color="#1688d4"/><stop offset="1" stop-color="#704ff2"/>
    </linearGradient>
  </defs>
  <rect width="256" height="256" fill="#111a27"/>
  <path fill="url(#af-cloud)" d="M184.8 193H74.2C48.1 193 27 172.2 27 146.4c0-23.1 17.1-42.5 39.8-46.1C73.9 70.7 99.4 49 130 49c36.1 0 65.5 29.1 65.9 65.1 19.3 4.9 33.1 22.2 33.1 42.5 0 20.1-16.3 36.4-36.2 36.4Z"/>
  <path fill="#f5fbff" d="M91 134.5a9 9 0 0 1 12.7 0l17.1 17.1a9 9 0 0 1 0 12.8l-17.1 17.1a9 9 0 1 1-12.7-12.8l10.7-10.7L91 147.3a9 9 0 0 1 0-12.8Zm73.1 0a9 9 0 0 1 12.8 12.8L166.2 158l10.7 10.7a9 9 0 1 1-12.8 12.8L147 164.4a9 9 0 0 1 0-12.8l17.1-17.1Z"/>
  <path fill="#dff5ff" d="M127 126.5h2a9 9 0 0 1 0 18h-2a9 9 0 0 1 0-18Zm0 43h2a9 9 0 0 1 0 18h-2a9 9 0 0 1 0-18Z"/>
</svg>`;

const copy: Record<'en' | 'es', Omit<Node, 'icon'>[]> = {
    en: [
        { id: 'local', title: 'Local app', desc: 'Dolphin or any KIO-aware program' },
        { id: 'kio', title: 'KIO', desc: "KDE's I/O framework" },
        { id: 'kio-rclone', title: 'KIO Rclone', desc: 'Translates KIO calls into rclone commands', highlight: true },
        { id: 'rclone', title: 'rclone', desc: 'Owns OAuth, retries and provider APIs' },
        { id: 'provider', title: 'Cloud provider', desc: 'Google Drive, S3, Dropbox, and more' },
    ],
    es: [
        { id: 'local', title: 'App local', desc: 'Dolphin o cualquier programa compatible con KIO' },
        { id: 'kio', title: 'KIO', desc: 'El framework de E/S de KDE' },
        { id: 'kio-rclone', title: 'KIO Rclone', desc: 'Traduce llamadas de KIO a comandos de rclone', highlight: true },
        { id: 'rclone', title: 'rclone', desc: 'Maneja OAuth, reintentos y las APIs de cada proveedor' },
        { id: 'provider', title: 'Proveedor cloud', desc: 'Google Drive, S3, Dropbox y más' },
    ],
};

const nodes: Node[] = copy[props.lang].map((node) => ({
    ...node,
    icon: node.highlight ? logoSvg : icons[node.id],
}));

const ariaLabel =
    props.lang === 'es'
        ? 'La app local conecta a través de KIO y KIO Rclone con rclone, que habla con el proveedor cloud'
        : 'Local app connects through KIO and KIO Rclone to rclone, which talks to the cloud provider';
</script>

<template>
    <div class="architecture-flow" :class="{ 'af-compact': compact }" role="img" :aria-label="ariaLabel">
        <template v-for="(node, index) in nodes" :key="node.id">
            <div class="af-card" :class="{ 'af-highlight': node.highlight, 'af-logo': node.highlight }" :title="compact ? `${node.title} — ${node.desc}` : undefined">
                <div class="af-icon">
                    <svg v-if="!node.highlight" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" v-html="node.icon" />
                    <div v-else class="af-logo-svg" v-html="node.icon" />
                </div>
                <template v-if="!compact">
                    <h3>{{ node.title }}</h3>
                    <p>{{ node.desc }}</p>
                </template>
            </div>
            <div v-if="index < nodes.length - 1" class="af-connector" aria-hidden="true">
                <svg viewBox="0 0 34 14">
                    <path d="M1 7h27" />
                    <polygon points="28,3 34,7 28,11" />
                </svg>
            </div>
        </template>
    </div>
</template>

<style scoped>
.architecture-flow {
    display: flex;
    align-items: stretch;
    justify-content: center;
    gap: 0;
    margin: 2rem 0;
}

.af-card {
    flex: 1 1 0;
    min-width: 0;
    background: var(--vp-c-bg-soft);
    border: 1px solid var(--vp-c-divider);
    border-radius: 10px;
    padding: 1.25rem 0.9rem;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 0.6rem;
}

.af-highlight {
    border-color: var(--vp-c-brand-1);
}

.af-icon {
    width: 48px;
    height: 48px;
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--vp-c-text-2);
}

.af-highlight .af-icon {
    color: var(--vp-c-brand-1);
}

.af-icon svg {
    width: 24px;
    height: 24px;
}

.af-logo-svg {
    width: 100%;
    height: 100%;
    display: flex;
    border-radius: 8px;
    overflow: hidden;
}

.af-logo-svg :deep(svg) {
    width: 100%;
    height: 100%;
    display: block;
}

.af-card h3 {
    margin: 0;
    padding: 0;
    border: none;
    font-size: 0.92rem;
    font-weight: 600;
    letter-spacing: -0.01em;
}

.af-highlight h3 {
    color: var(--vp-c-brand-1);
}

.af-card p {
    margin: 0;
    font-size: 0.74rem;
    line-height: 1.4;
    color: var(--vp-c-text-2);
}

.af-connector {
    flex: 0 0 auto;
    width: 34px;
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--vp-c-divider);
}

.af-connector svg {
    width: 100%;
    height: 14px;
    overflow: visible;
}

.af-connector path {
    stroke: currentColor;
    stroke-width: 1.5;
    fill: none;
}

.af-connector polygon {
    fill: currentColor;
}

/* Compact: icon-only cards, no title/description, used inline in prose. */
.af-compact {
    max-width: 560px;
    margin-left: auto;
    margin-right: auto;
}

.af-compact .af-card {
    flex: 0 0 auto;
    width: 52px;
    height: 52px;
    padding: 0;
}

.af-compact .af-card.af-logo {
    overflow: hidden;
    border-radius: 10px;
}

.af-compact .af-icon {
    width: 100%;
    height: 100%;
}

.af-compact .af-connector {
    flex: 1 1 0;
    min-width: 16px;
}

@media (max-width: 720px) {
    .architecture-flow:not(.af-compact) {
        flex-direction: column;
        align-items: stretch;
    }

    .architecture-flow:not(.af-compact) .af-connector {
        width: auto;
        height: 20px;
        transform: rotate(90deg);
    }
}
</style>
