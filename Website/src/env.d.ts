/// <reference types="astro/client" />

type Runtime = import('@astrojs/cloudflare').Runtime<Env>;

interface Env {
	DB: D1Database;
	SESSION_STORE: KVNamespace;
	GITHUB_CLIENT_ID: string;
	GITHUB_CLIENT_SECRET: string;
}

declare namespace App {
	interface Locals extends Runtime {
		user: {
			id: string;
			username: string;
			avatarUrl: string;
		} | null;
	}
}
