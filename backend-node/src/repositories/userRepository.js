import { withTransaction } from './transaction.js';

export class UserRepository {
  constructor(database) {
    this.database = database;
  }

  async createCustomer({ userId, name, email, passwordHash }) {
    return withTransaction(this.database, async (client) => {
      const result = await client.query(
        'INSERT INTO users '
          + '(user_id, name, email, password_hash, role, membership_level) '
          + "VALUES ($1, $2, $3, $4, 'CUSTOMER', 'Regular') "
          + 'RETURNING user_id, name, email, role',
        [userId, name, email, passwordHash],
      );
      return result.rows[0];
    });
  }

  async findByEmail(email) {
    const result = await this.database.query(
      'SELECT user_id, name, email, password_hash, role '
        + 'FROM users WHERE email = $1',
      [email],
    );
    return result.rows[0] ?? null;
  }

  async createInitialAdmin({ userId, name, email, passwordHash }, createSession) {
    return withTransaction(this.database, async (client) => {
      await client.query('SELECT pg_advisory_xact_lock(1448230227, 1229863252)');

      const admins = await client.query(
        "SELECT user_id FROM users WHERE role = 'ADMIN' LIMIT 1",
      );
      if (admins.rows.length > 0) return { alreadyProvisioned: true };

      const existing = await client.query(
        'SELECT user_id FROM users WHERE user_id = $1 OR email = $2',
        [userId, email],
      );
      if (existing.rows.length > 0) return { duplicate: true };

      const inserted = await client.query(
        'INSERT INTO users '
          + '(user_id, name, email, password_hash, role, membership_level) '
          + "VALUES ($1, $2, $3, $4, 'ADMIN', 'Regular') "
          + 'RETURNING user_id, name, email, role',
        [userId, name, email, passwordHash],
      );
      const user = inserted.rows[0];
      const sessionId = await createSession(client, user.user_id, user.role);
      return { user, sessionId };
    });
  }

  async createAdmin({ userId, name, email, passwordHash }, actorId) {
    return withTransaction(this.database, async (client) => {
      const actor = await client.query(
        'SELECT user_id, role FROM users WHERE user_id = $1 FOR SHARE',
        [actorId],
      );
      if (!actor.rows[0] || actor.rows[0].role !== 'ADMIN') {
        return { unauthorized: true };
      }

      const existing = await client.query(
        'SELECT user_id FROM users WHERE user_id = $1 OR email = $2',
        [userId, email],
      );
      if (existing.rows.length > 0) return { duplicate: true };

      const inserted = await client.query(
        'INSERT INTO users '
          + '(user_id, name, email, password_hash, role, membership_level) '
          + "VALUES ($1, $2, $3, $4, 'ADMIN', 'Regular') "
          + 'RETURNING user_id, name, email, role',
        [userId, name, email, passwordHash],
      );
      return { user: inserted.rows[0] };
    });
  }
}
