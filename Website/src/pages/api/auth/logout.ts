export const prerender = false;

import type { APIRoute } from 'astro';
import { destroySession } from '../../../lib/session';

export const POST: APIRoute = async ({ locals, cookies, redirect }) => {
	const token = cookies.get('sol_session')?.value;
	if (token) {
		const env = locals.runtime?.env;
		if (env?.SESSION_STORE) await destroySession(env.SESSION_STORE, token);
	}
	cookies.delete('sol_session', { path: '/' });
	return redirect('/');
};
