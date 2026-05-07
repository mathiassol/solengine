// Post-processes the adapter-generated wrangler.json to be Cloudflare Pages compatible.
import { readFileSync, writeFileSync } from 'fs';

const files = [
	'dist/server/wrangler.json',
	'dist/server/.prerender/wrangler.json',
];

for (const path of files) {
	let raw;
	try {
		raw = readFileSync(path, 'utf8');
	} catch {
		continue; // file doesn't exist, skip
	}

	const cfg = JSON.parse(raw);

	// ASSETS is reserved in Pages — remove explicit declaration (Pages provides it automatically)
	delete cfg.assets;

	// Remove KV namespaces with no id (adapter auto-provisioning not supported in Pages)
	if (Array.isArray(cfg.kv_namespaces)) {
		cfg.kv_namespaces = cfg.kv_namespaces.filter((kv) => kv.id);
	}

	// triggers must be { crons: [...] }, not {}
	if (cfg.triggers !== undefined) {
		cfg.triggers = { crons: cfg.triggers?.crons ?? [] };
	}

	// Remove previews section (not valid in Pages Worker config)
	delete cfg.previews;

	writeFileSync(path, JSON.stringify(cfg, null, 2));
	console.log(`Patched ${path}`);
}
