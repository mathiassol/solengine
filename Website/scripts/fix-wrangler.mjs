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
		continue;
	}

	const cfg = JSON.parse(raw);

	// ASSETS is reserved in Pages — remove explicit declaration
	delete cfg.assets;

	// Remove KV namespaces with no id, then deduplicate by binding name
	if (Array.isArray(cfg.kv_namespaces)) {
		const seen = new Set();
		cfg.kv_namespaces = cfg.kv_namespaces.filter((kv) => {
			if (!kv.id) return false;
			if (seen.has(kv.binding)) return false;
			seen.add(kv.binding);
			return true;
		});
	}

	// Deduplicate D1 databases by binding name
	if (Array.isArray(cfg.d1_databases)) {
		const seen = new Set();
		cfg.d1_databases = cfg.d1_databases.filter((db) => {
			if (seen.has(db.binding)) return false;
			seen.add(db.binding);
			return true;
		});
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
