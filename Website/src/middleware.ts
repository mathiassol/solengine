import { defineMiddleware } from 'astro:middleware';

export const onRequest = defineMiddleware(async (context, next) => {
	context.locals.user = null;

	const token = context.cookies.get('sol_session')?.value;
	if (token) {
		try {
			const env = context.locals.runtime?.env;
			if (env?.SESSION_STORE) {
				const raw = await env.SESSION_STORE.get(`session:${token}`);
				if (raw) context.locals.user = JSON.parse(raw);
			}
		} catch {
			// expired or malformed session — ignore
		}
	}

	return next();
});
