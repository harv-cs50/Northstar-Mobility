function maintenanceFromRow(row) {
  return {
    maintenanceId: row.maintenance_id,
    vehicleId: row.vehicle_id,
    serviceDate: row.service_date,
    description: row.description,
    status: row.status,
    maintenanceCost: Number(row.maintenance_cost),
    completionDate: row.completion_date ?? '',
  };
}

function rentalFromRow(row) {
  return {
    rentalId: row.rental_id,
    bookingId: row.booking_id,
    plannedReturnDate: row.planned_return_date,
    actualReturnDate: row.actual_return_date ?? '',
    dailyLateFee: Number(row.daily_late_fee),
    returned: row.actual_return_date !== null && row.actual_return_date !== undefined,
  };
}

export class AdminOverviewRepository {
  constructor(database) {
    this.database = database;
  }

  async getAllMaintenance() {
    const result = await this.database.query(
      'SELECT maintenance_id, vehicle_id, '
        + "to_char(service_date, 'YYYY-MM-DD') AS service_date, "
        + 'description, status, maintenance_cost, '
        + "COALESCE(to_char(completion_date, 'YYYY-MM-DD'), '') AS completion_date "
        + 'FROM maintenance_records ORDER BY service_date DESC',
    );
    return result.rows.map(maintenanceFromRow);
  }

  async getAllRentals() {
    const result = await this.database.query(
      'SELECT rental_id, booking_id, '
        + "to_char(planned_return_date, 'YYYY-MM-DD') AS planned_return_date, "
        + "to_char(actual_return_date, 'YYYY-MM-DD') AS actual_return_date, "
        + 'daily_late_fee '
        + 'FROM rentals ORDER BY rental_id',
    );
    return result.rows.map(rentalFromRow);
  }
}
