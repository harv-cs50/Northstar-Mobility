import { randomUUID } from 'node:crypto';

export class AuditLogRepository {
  constructor(database) {
    this.database = database;
  }

  async write(userId, action, description) {
    await this.database.query(
      'INSERT INTO audit_logs (log_id, user_id, action, description) '
        + 'VALUES ($1, $2, $3, $4)',
      [randomUUID(), userId || 'UNKNOWN', action, description],
    );
  }
}
