// Post-processes the adapter-generated wrangler.json to be Cloudflare Pages compatible.
import { readFileSync, writeFileSync } from 'fs';

const files = [
	'dist/server/wrangler.json',
	'dist/server/.prerender/wrangler.json',
];

// Fields only valid for Workers, not Pages
const WORKER_ONLY_FIELDS = [
	'main', 'rules', 'no_bundle', 'build', 'assets',
	'previews', 'jsx_factory', 'jsx_fragment', 'migrations',
	'topLevelName', 'definedEnvironments',
	'ai_search_namespaces', 'ai_search', 'secrets_store_secrets',
	'artifacts', 'unsafe_hello_world', 'flagship', 'worker_loaders',
	'ratelimits', 'vpc_services', 'vpc_networks', 'python_modules',
];

for (const path of files) {
	let raw;
	try {
		raw = readFileSync(path, 'utf8');
	} catch {
		continue;
	}

	const cfg = JSON.parse(raw);

	// Strip all Worker-only fields
	for (const field of WORKER_ONLY_FIELDS) {
		delete cfg[field];
	}

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

	// dev field: strip unsupported keys
	if (cfg.dev) {
		delete cfg.dev.enable_containers;
		delete cfg.dev.generate_types;
	}

	// Fix pages_build_output_dir — remove from generated config (absolute local path is wrong on CI)
	// The original wrangler.toml already declares the correct value; Pages uses that.
	delete cfg.pages_build_output_dir;

	writeFileSync(path, JSON.stringify(cfg, null, 2));
	console.log(`Patched ${path}`);
}
