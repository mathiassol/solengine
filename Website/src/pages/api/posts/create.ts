export const prerender = false;

import type { APIRoute } from 'astro';
import { nanoid } from '../../../lib/nanoid';

function slugify(text: string): string {
	return text
		.toLowerCase()
		.replace(/[^a-z0-9\s-]/g, '')
		.trim()
		.replace(/\s+/g, '-')
		.slice(0, 80);
}

export const POST: APIRoute = async ({ request, locals }) => {
	const user = locals.user;
	if (!user) return new Response('Unauthorized', { status: 401 });

	const body = await request.json() as { title?: string; content?: string; category?: string };
	const { title, content, category } = body;

	if (!title?.trim() || !content?.trim())
		return new Response('Missing title or content', { status: 400 });

	const validCategories = ['engine', 'games', 'general'];
	if (!validCategories.includes(category ?? ''))
		return new Response('Invalid category', { status: 400 });

	const env = locals.runtime?.env;
	if (!env?.DB) return new Response('DB not available', { status: 500 });

	const id = nanoid();
	const slug = `${slugify(title)}-${id.slice(0, 6)}`;

	await env.DB.prepare(
		`INSERT INTO posts (id, title, content, author_id, category, slug, created_at)
		 VALUES (?, ?, ?, ?, ?, ?, ?)`,
	).bind(id, title.trim(), content.trim(), user.id, category, slug, Date.now()).run();

	return Response.json({ slug });
};
