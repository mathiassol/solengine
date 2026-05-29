// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import cloudflare from '@astrojs/cloudflare';

// https://astro.build/config
export default defineConfig({
	output: 'static',
	adapter: cloudflare({
		imageService: 'passthrough',
		prerenderEnvironment: 'node',
	}),
	integrations: [
		starlight({
			title: 'SolEngine',
			social: [{ icon: 'github', label: 'GitHub', href: 'https://github.com/mathiassol/solengine' }],
			components: {
				Header: './src/components/StarlightHeader.astro',
			},
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
					items: [
						{ label: 'Node Types', slug: 'reference/node-types' },
						{ label: 'Lua API', slug: 'reference/lua-api' },
						{ label: 'Material Editor', slug: 'reference/material-editor' },
					],
				},
			],
			customCss: ['./src/styles/custom.css'],
		}),
	],
});
