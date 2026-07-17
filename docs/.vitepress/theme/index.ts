import DefaultTheme from 'vitepress/theme';
import ArchitectureFlow from './ArchitectureFlow.vue';
import './custom.css';
import type { Theme } from 'vitepress';

export default {
    extends: DefaultTheme,
    enhanceApp({ app }) {
        app.component('ArchitectureFlow', ArchitectureFlow);
    },
} satisfies Theme;
