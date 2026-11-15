import { createHash, randomBytes } from 'node:crypto';

const sessionLifetimeSql = "INTERVAL '30 minutes'";

export function hashSessionToken(sessionToken) {
  return createHash('sha256').update(sessionToken, 'utf8').digest('hex');
}

export class SessionRepository {
  constructor(database) {
    this.database = database;
  }

  async createSession(userId, role, queryable = this.database) {
    const sessionToken = randomBytes(32).toString('hex');
    await queryable.query(
      'INSERT INTO auth_sessions '
        + '(session_token_hash, user_id, role, created_at, last_activity_at, expires_at) '
        + `VALUES ($1, $2, $3, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP + ${sessionLifetimeSql})`,
      [hashSessionToken(sessionToken), userId, role],
    );
    return sessionToken;
  }

  async getAndTouch(sessionToken) {
    if (!sessionToken) return null;
    const result = await this.database.query(
      'UPDATE auth_sessions AS s '
        + 'SET last_activity_at = CURRENT_TIMESTAMP, '
        + `expires_at = CURRENT_TIMESTAMP + ${sessionLifetimeSql} `
        + 'FROM users AS u '
        + 'WHERE s.session_token_hash = $1 '
        + 'AND s.expires_at > CURRENT_TIMESTAMP '
        + 'AND u.user_id = s.user_id '
        + 'AND u.role = s.role '
        + 'RETURNING s.user_id, s.role',
      [hashSessionToken(sessionToken)],
    );
    const row = result.rows[0];
    return row ? { userId: row.user_id, role: row.role } : null;
  }

  async deleteSession(sessionToken) {
    if (!sessionToken) return false;
    const result = await this.database.query(
      'DELETE FROM auth_sessions WHERE session_token_hash = $1 RETURNING user_id',
      [hashSessionToken(sessionToken)],
    );
    return result.rowCount === 1;
  }
}
