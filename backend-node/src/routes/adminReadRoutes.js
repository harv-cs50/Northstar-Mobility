import { Router } from 'express';

export function createAdminReadRouter({ bookingService, overviewRepository, authorization }) {
  const router = Router();

  router.get('/bookings', authorization.authenticate, authorization.requireAdmin,
    async (request, response, next) => {
      try {
        return response.status(200).json(await bookingService.getBookings(request.principal));
      } catch (error) {
        return next(error);
      }
    });

  router.get('/maintenance', authorization.authenticate, authorization.requireAdmin,
    async (request, response, next) => {
      try {
        return response.status(200).json(await overviewRepository.getAllMaintenance());
      } catch (error) {
        return next(error);
      }
    });

  router.get('/rentals', authorization.authenticate, authorization.requireAdmin,
    async (request, response, next) => {
      try {
        return response.status(200).json(await overviewRepository.getAllRentals());
      } catch (error) {
        return next(error);
      }
    });

  return router;
}
