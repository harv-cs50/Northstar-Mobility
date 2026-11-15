function bearerToken(request) {
  const authorization = request.get('Authorization') ?? '';
  const match = authorization.match(/^Bearer\s+(.+)$/i);
  return match?.[1]?.trim() ?? '';
}

export function createAuthorizationMiddleware(sessions) {
  const authenticate = async (request, response, next) => {
    const sessionToken = bearerToken(request);
    request.sessionToken = sessionToken;
    request.principal = null;

    if (!sessionToken) return next();
    try {
      request.principal = await sessions.getAndTouch(sessionToken);
      return next();
    } catch {
      return next(Object.assign(new Error('Authentication service is not available'), {
        status: 503,
      }));
    }
  };

  const requireAuthenticated = (request, response, next) => {
    if (!request.principal) {
      return response.status(401).json({ error: 'Authentication required' });
    }
    return next();
  };

  const requireRole = (role, message) => (request, response, next) => {
    if (!request.principal) {
      return response.status(401).json({ error: 'Authentication required' });
    }
    if (request.principal.role !== role) {
      return response.status(403).json({ error: message });
    }
    return next();
  };

  return {
    authenticate,
    requireAuthenticated,
    requireCustomer: requireRole('CUSTOMER', 'Customer account required'),
    requireAdmin: requireRole('ADMIN', 'Administrator account required'),
  };
}
