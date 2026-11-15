-- PostgreSQL-backed opaque sessions for the Node API.
-- Apply only after the existing users table is present. This migration is not
-- executed automatically by the API and has not been run against any database.
BEGIN;

CREATE TABLE IF NOT EXISTS auth_sessions (
    session_token_hash text PRIMARY KEY
        CHECK (session_token_hash ~ '^[0-9a-f]{64}$'),
    user_id varchar NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    role varchar NOT NULL CHECK (role IN ('CUSTOMER', 'ADMIN')),
    created_at timestamp with time zone NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_activity_at timestamp with time zone NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamp with time zone NOT NULL,
    CHECK (expires_at > created_at)
);

CREATE INDEX IF NOT EXISTS auth_sessions_expires_at_idx
    ON auth_sessions (expires_at);
CREATE INDEX IF NOT EXISTS auth_sessions_user_id_idx
    ON auth_sessions (user_id);

COMMIT;
