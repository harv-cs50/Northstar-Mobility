import { Router } from 'express';

export function createBookingRouter({ bookingService, authorization }) {
  const router = Router();

  router.get('/', authorization.authenticate, authorization.requireAuthenticated,
    async (request, response, next) => {
      try {
        const bookings = await bookingService.getBookings(request.principal);
        return response.status(200).json(bookings);
      } catch (error) {
        return next(error);
      }
    });

  router.post('/', authorization.authenticate, authorization.requireCustomer,
    async (request, response, next) => {
      try {
        const booking = await bookingService.createBooking(
          request.principal.userId,
          request.body ?? {},
        );
        return response.status(201).json(booking);
      } catch (error) {
        return next(error);
      }
    });

  router.post('/:bookingId/cancel', authorization.authenticate,
    authorization.requireAuthenticated, async (request, response, next) => {
      try {
        const result = await bookingService.cancelBooking(
          request.params.bookingId,
          request.principal,
        );
        return response.status(200).json(result);
      } catch (error) {
        return next(error);
      }
    });

  return router;
}
