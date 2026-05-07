export const prerender = false;

import type { APIRoute } from 'astro';

export const GET: APIRoute = ({ redirect, locals }) => {
	const env = locals.runtime?.env;
	if (!env?.GITHUB_CLIENT_ID) return new Response('OAuth not configured', { status: 500 });

	const params = new URLSearchParams({
		client_id: env.GITHUB_CLIENT_ID,
		scope: 'read:user',
		redirect_uri: `${new URL(import.meta.env.SITE ?? 'http://localhost:4321').origin}/api/auth/callback`,
	});

	return redirect(`https://github.com/login/oauth/authorize?${params}`);
};
