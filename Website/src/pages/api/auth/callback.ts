export const prerender = false;

import type { APIRoute } from 'astro';
import { createSession } from '../../../lib/session';
import { nanoid } from '../../../lib/nanoid';

export const GET: APIRoute = async ({ url, locals, cookies, redirect }) => {
	const code = url.searchParams.get('code');
	if (!code) return new Response('Missing code', { status: 400 });

	const env = locals.runtime?.env;
	if (!env?.GITHUB_CLIENT_ID || !env?.GITHUB_CLIENT_SECRET)
		return new Response('OAuth not configured', { status: 500 });

	// Exchange code for token
	const tokenRes = await fetch('https://github.com/login/oauth/access_token', {
		method: 'POST',
		headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
		body: JSON.stringify({
			client_id: env.GITHUB_CLIENT_ID,
			client_secret: env.GITHUB_CLIENT_SECRET,
			code,
		}),
	});
	const tokenData = await tokenRes.json() as { access_token?: string };
	if (!tokenData.access_token) return new Response('OAuth token exchange failed', { status: 502 });

	// Fetch GitHub user
	const userRes = await fetch('https://api.github.com/user', {
		headers: {
			Authorization: `Bearer ${tokenData.access_token}`,
			'User-Agent': 'solengine-site',
		},
	});
	const ghUser = await userRes.json() as { id: number; login: string; avatar_url: string };

	// Upsert user in D1
	await env.DB.prepare(
		`INSERT INTO users (id, github_id, username, avatar_url, created_at)
		 VALUES (?, ?, ?, ?, ?)
		 ON CONFLICT (github_id) DO UPDATE SET username = excluded.username, avatar_url = excluded.avatar_url`,
	).bind(nanoid(), ghUser.id, ghUser.login, ghUser.avatar_url, Date.now()).run();

	const row = await env.DB.prepare('SELECT id FROM users WHERE github_id = ?')
		.bind(ghUser.id)
		.first<{ id: string }>();

	const sessionUser = { id: row!.id, username: ghUser.login, avatarUrl: ghUser.avatar_url };
	const token = await createSession(env.SESSION_STORE, sessionUser);

	cookies.set('sol_session', token, {
		path: '/',
		httpOnly: true,
		secure: true,
		sameSite: 'lax',
		maxAge: 60 * 60 * 24 * 30,
	});

	return redirect('/forum');
};
