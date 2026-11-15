import { z } from 'zod';

const vehicleInputSchema = z.object({
  id: z.string().min(1),
  brand: z.string().min(1),
  model: z.string().min(1),
  type: z.string().min(1),
  dailyRate: z.union([z.number(), z.string()]),
});

export class VehicleError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function parseVehicle(input) {
  const parsed = vehicleInputSchema.safeParse(input);
  if (!parsed.success) {
    const rateIssue = parsed.error.issues.some((issue) => issue.path[0] === 'dailyRate');
    throw new VehicleError(400,
      rateIssue ? 'dailyRate must be a positive number' : 'Invalid vehicle details');
  }

  const dailyRate = typeof parsed.data.dailyRate === 'string'
    ? Number(parsed.data.dailyRate.trim())
    : parsed.data.dailyRate;
  if (!Number.isFinite(dailyRate)) {
    throw new VehicleError(400, 'dailyRate must be a positive number');
  }
  const roundedRate = Math.round(dailyRate * 100) / 100;
  if (dailyRate <= 0 || dailyRate > 9_999_999_999.99 || roundedRate <= 0) {
    throw new VehicleError(400, 'Invalid vehicle details');
  }

  return { ...parsed.data, dailyRate };
}

function databaseUnavailable() {
  return new VehicleError(503, 'Vehicle service is not available');
}

export class VehicleService {
  constructor(repository) {
    this.repository = repository;
  }

  async getAvailableVehicles() {
    try {
      return await this.repository.getAvailableVehicles();
    } catch {
      throw databaseUnavailable();
    }
  }

  async getAllVehicles() {
    try {
      return await this.repository.getAllVehicles();
    } catch {
      throw databaseUnavailable();
    }
  }

  async createVehicle(input) {
    const vehicle = parseVehicle(input);
    try {
      await this.repository.createVehicle(vehicle);
      return { success: true };
    } catch (error) {
      if (error?.code === '23505') {
        throw new VehicleError(409, 'Vehicle ID already exists');
      }
      throw databaseUnavailable();
    }
  }
}
