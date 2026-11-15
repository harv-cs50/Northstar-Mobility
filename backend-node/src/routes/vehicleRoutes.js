import { Router } from 'express';

export function createPublicVehicleRouter(vehicleService) {
  const router = Router();
  router.get('/', async (request, response, next) => {
    try {
      const vehicles = await vehicleService.getAvailableVehicles();
      return response.status(200).json(vehicles);
    } catch (error) {
      return next(error);
    }
  });
  return router;
}

export function createAdminVehicleRouter({ vehicleService, authorization }) {
  const router = Router();
  router.use(authorization.authenticate, authorization.requireAdmin);

  router.get('/', async (request, response, next) => {
    try {
      const vehicles = await vehicleService.getAllVehicles();
      return response.status(200).json(vehicles);
    } catch (error) {
      return next(error);
    }
  });

  router.post('/', async (request, response, next) => {
    try {
      const result = await vehicleService.createVehicle(request.body);
      return response.status(201).json(result);
    } catch (error) {
      return next(error);
    }
  });

  return router;
}
