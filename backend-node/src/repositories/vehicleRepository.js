function vehicleFromRow(row) {
  return {
    id: row.vehicle_id,
    brand: row.brand,
    model: row.model,
    type: row.vehicle_type,
    dailyRate: Number(row.daily_rate),
    available: row.is_available,
  };
}

export class VehicleRepository {
  constructor(database) {
    this.database = database;
  }

  async getAvailableVehicles() {
    const result = await this.database.query(
      'SELECT vehicle_id, brand, model, vehicle_type, daily_rate, is_available '
        + 'FROM vehicles WHERE is_available = TRUE ORDER BY vehicle_id',
    );
    return result.rows.map(vehicleFromRow);
  }

  async getAllVehicles() {
    const result = await this.database.query(
      'SELECT vehicle_id, brand, model, vehicle_type, daily_rate, is_available '
        + 'FROM vehicles ORDER BY vehicle_id',
    );
    return result.rows.map(vehicleFromRow);
  }

  async createVehicle({ id, brand, model, type, dailyRate }) {
    await this.database.query(
      'INSERT INTO vehicles '
        + '(vehicle_id, brand, model, vehicle_type, daily_rate) '
        + 'VALUES ($1, $2, $3, $4, $5)',
      [id, brand, model, type, dailyRate],
    );
  }
}
