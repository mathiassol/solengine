// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// https://astro.build/config
export default defineConfig({
	integrations: [
		starlight({
			title: 'SolEngine',
			social: [{ icon: 'github', label: 'GitHub', href: 'https://github.com/mathiassol/solengine' }],
			sidebar: [
				{
					label: 'Getting Started',
					items: [
						{ label: 'Introduction', slug: 'docs' },
						{ label: 'Getting Started', slug: 'docs/getting-started' },
					],
				},
				{
					label: 'Reference',
					autogenerate: { directory: 'docs/reference' },
				},
			],
			customCss: ['./src/styles/custom.css'],
		}),
	],
});
