import { Router } from 'express';

export function createAuthRouter({ authService, authorization }) {
  const router = Router();

  router.post('/register', async (request, response, next) => {
    try {
      const result = await authService.registerCustomer(request.body);
      return response.status(result.status).json(result.body);
    } catch (error) {
      return next(error);
    }
  });

  router.post('/login', async (request, response, next) => {
    try {
      const { email, password } = request.body ?? {};
      const result = await authService.login(email, password);
      return response.status(result.status).json(result.body);
    } catch (error) {
      return next(error);
    }
  });

  router.post('/bootstrap-admin', async (request, response, next) => {
    try {
      const result = await authService.bootstrapAdmin(
        request.body,
        request.get('X-Admin-Bootstrap-Token') ?? '',
      );
      return response.status(result.status).json(result.body);
    } catch (error) {
      return next(error);
    }
  });

  router.post('/logout', authorization.authenticate,
    authorization.requireAuthenticated, async (request, response, next) => {
      try {
        const result = await authService.logout(
          request.sessionToken,
          request.principal.userId,
        );
        return response.status(result.status).json(result.body);
      } catch (error) {
        return next(error);
      }
    });

  return router;
}

export function createAdminUserRouter({ authService, authorization }) {
  const router = Router();
  router.post('/users', authorization.authenticate, authorization.requireAdmin,
    async (request, response, next) => {
      try {
        const result = await authService.createAdmin(
          request.body,
          request.principal.userId,
        );
        return response.status(result.status).json(result.body);
      } catch (error) {
        return next(error);
      }
    });
  return router;
}
