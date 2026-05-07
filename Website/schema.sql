-- Run once via: wrangler d1 execute solengine-forum --file=schema.sql

CREATE TABLE IF NOT EXISTS users (
	id         TEXT    PRIMARY KEY,
	github_id  INTEGER UNIQUE NOT NULL,
	username   TEXT    NOT NULL,
	avatar_url TEXT    NOT NULL DEFAULT '',
	created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS posts (
	id         TEXT    PRIMARY KEY,
	title      TEXT    NOT NULL,
	content    TEXT    NOT NULL,
	author_id  TEXT    NOT NULL REFERENCES users(id),
	category   TEXT    NOT NULL CHECK (category IN ('engine', 'games', 'general')),
	slug       TEXT    UNIQUE NOT NULL,
	created_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS posts_created_at ON posts (created_at DESC);
CREATE INDEX IF NOT EXISTS posts_category   ON posts (category);
