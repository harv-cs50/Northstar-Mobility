import { timingSafeEqual } from 'node:crypto';
import { hashPassword, verifyPassword } from '../auth/passwordHasher.js';
import { isValidLoginInput, parseRegistration } from '../auth/validation.js';

export class AuthError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function tokenMatches(provided, configured) {
  if (typeof provided !== 'string' || typeof configured !== 'string' ||
      !provided || !configured) return false;
  const left = Buffer.from(provided, 'utf8');
  const right = Buffer.from(configured, 'utf8');
  return left.length === right.length && timingSafeEqual(left, right);
}

function databaseFailure(error, unavailableMessage) {
  if (error?.code === '23505') {
    throw new AuthError(409, 'User ID or email already exists');
  }
  throw new AuthError(503, unavailableMessage);
}

export class AuthService {
  constructor({ users, sessions, audit, bootstrapToken = '' }) {
    this.users = users;
    this.sessions = sessions;
    this.audit = audit;
    this.bootstrapToken = bootstrapToken;
  }

  async recordAudit(userId, action, description) {
    try {
      await this.audit?.write(userId, action, description);
    } catch {
      // Match the existing C++ authentication behavior: audit outages do not
      // turn a successful login or registration into an authentication failure.
    }
  }

  async registerCustomer(input) {
    const parsed = parseRegistration(input);
    if (!parsed.success) {
      await this.recordAudit(input?.userId, 'REGISTRATION_FAILED',
        'Customer registration rejected due to invalid details');
      throw new AuthError(400, 'Invalid registration details');
    }

    const data = parsed.data;
    try {
      const passwordHash = await hashPassword(data.password);
      const user = await this.users.createCustomer({ ...data, passwordHash });
      await this.recordAudit(user.user_id, 'CUSTOMER_REGISTERED',
        'Customer account registered successfully');
      return {
        status: 201,
        body: {
          userId: user.user_id,
          name: user.name,
          role: 'CUSTOMER',
          message: 'Customer registered successfully',
        },
      };
    } catch (error) {
      if (error instanceof AuthError) throw error;
      await this.recordAudit(data.userId, 'REGISTRATION_FAILED',
        'Customer registration failed due to a system error');
      databaseFailure(error, 'Registration service is not available');
    }
  }

  async login(email, password) {
    if (!isValidLoginInput(email, password)) {
      await this.recordAudit('UNKNOWN', 'LOGIN_FAILED', 'Login rejected due to invalid input');
      throw new AuthError(401, 'Invalid email or password');
    }

    let user;
    try {
      user = await this.users.findByEmail(email);
    } catch {
      await this.recordAudit('UNKNOWN', 'LOGIN_ERROR',
        'Login could not be completed due to a system error');
      throw new AuthError(503, 'Authentication service is not available');
    }

    if (!user || !['CUSTOMER', 'ADMIN'].includes(user.role) ||
        !(await verifyPassword(password, user.password_hash))) {
      await this.recordAudit(user?.user_id ?? 'UNKNOWN', 'LOGIN_FAILED',
        user ? 'Login rejected due to incorrect password' : 'Login rejected because the account was not found');
      throw new AuthError(401, 'Invalid email or password');
    }

    try {
      const sessionId = await this.sessions.createSession(user.user_id, user.role);
      await this.recordAudit(user.user_id, 'LOGIN_SUCCESS', 'User logged in successfully');
      return {
        status: 200,
        body: {
          userId: user.user_id,
          name: user.name,
          role: user.role,
          sessionId,
        },
      };
    } catch {
      await this.recordAudit('UNKNOWN', 'LOGIN_ERROR',
        'Login could not be completed due to a system error');
      throw new AuthError(503, 'Authentication service is not available');
    }
  }

  async bootstrapAdmin(input, providedToken) {
    if (!tokenMatches(providedToken, this.bootstrapToken)) {
      await this.recordAudit('UNKNOWN', 'ADMIN_BOOTSTRAP_FAILED',
        'Initial admin provisioning rejected by bootstrap authorization');
      throw new AuthError(403, 'Initial admin provisioning is not authorized');
    }

    const parsed = parseRegistration(input);
    if (!parsed.success) {
      await this.recordAudit('UNKNOWN', 'ADMIN_BOOTSTRAP_FAILED',
        'Initial admin provisioning rejected due to invalid details');
      throw new AuthError(400, 'Invalid admin details');
    }

    const data = parsed.data;
    try {
      const passwordHash = await hashPassword(data.password);
      const result = await this.users.createInitialAdmin(
        { ...data, passwordHash },
        (client, userId, role) => this.sessions.createSession(userId, role, client),
      );
      if (result.alreadyProvisioned) {
        await this.recordAudit('UNKNOWN', 'ADMIN_BOOTSTRAP_FAILED',
          'Initial admin provisioning rejected because an admin already exists');
        throw new AuthError(409, 'Initial admin has already been provisioned');
      }
      if (result.duplicate) throw new AuthError(409, 'User ID or email already exists');

      await this.recordAudit(result.user.user_id, 'ADMIN_BOOTSTRAPPED',
        'Initial administrator account provisioned successfully');
      return {
        status: 201,
        body: {
          userId: result.user.user_id,
          name: result.user.name,
          role: 'ADMIN',
          sessionId: result.sessionId,
        },
      };
    } catch (error) {
      if (error instanceof AuthError) throw error;
      await this.recordAudit('UNKNOWN', 'ADMIN_BOOTSTRAP_FAILED',
        'Initial admin provisioning failed due to a system error');
      databaseFailure(error, 'Initial admin provisioning is not available');
    }
  }

  async createAdmin(input, actorId) {
    const parsed = parseRegistration(input);
    if (!parsed.success) {
      await this.recordAudit(actorId, 'ADMIN_REGISTRATION_FAILED',
        'Admin registration rejected due to invalid details');
      throw new AuthError(400, 'Invalid admin details');
    }

    const data = parsed.data;
    try {
      const passwordHash = await hashPassword(data.password);
      const result = await this.users.createAdmin({ ...data, passwordHash }, actorId);
      if (result.unauthorized) {
        await this.recordAudit(actorId, 'ADMIN_REGISTRATION_FAILED',
          'Admin provisioning rejected because the session is no longer authorized');
        throw new AuthError(403, 'Administrator authorization required');
      }
      if (result.duplicate) throw new AuthError(409, 'User ID or email already exists');

      await this.recordAudit(actorId, 'ADMIN_REGISTERED',
        'Administrator account provisioned successfully');
      return {
        status: 201,
        body: { userId: result.user.user_id, role: 'ADMIN' },
      };
    } catch (error) {
      if (error instanceof AuthError) throw error;
      await this.recordAudit(actorId, 'ADMIN_REGISTRATION_FAILED',
        'Admin registration failed due to a system error');
      databaseFailure(error, 'Admin provisioning is not available');
    }
  }

  async logout(sessionToken, userId) {
    try {
      const deleted = await this.sessions.deleteSession(sessionToken);
      if (!deleted) throw new AuthError(401, 'Session is invalid or expired');
      await this.recordAudit(userId, 'LOGOUT_SUCCESS', 'User logged out successfully');
      return { status: 200, body: { success: true } };
    } catch (error) {
      if (error instanceof AuthError) throw error;
      throw new AuthError(503, 'Authentication service is not available');
    }
  }
}
