export function generateToken(): string {
	const bytes = new Uint8Array(32);
	crypto.getRandomValues(bytes);
	return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

export async function createSession(
	kv: KVNamespace,
	user: { id: string; username: string; avatarUrl: string },
): Promise<string> {
	const token = generateToken();
	await kv.put(`session:${token}`, JSON.stringify(user), {
		expirationTtl: 60 * 60 * 24 * 30, // 30 days
	});
	return token;
}

export async function destroySession(kv: KVNamespace, token: string): Promise<void> {
	await kv.delete(`session:${token}`);
}
