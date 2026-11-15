# Booking PostgreSQL integration test plan

This is a plan only. It does not create a database, connect to PostgreSQL, run SQL, or apply migrations.

## Isolation and setup

1. Use a disposable database named `vrms_node_booking_test` on a local or CI PostgreSQL instance. Do not use `vehicle_rental_db` or any other application database.
2. Create that database through an explicitly selected test-server connection. Before schema setup or cleanup, run `SELECT current_database()` and require the exact result `vrms_node_booking_test`; abort otherwise.
3. Initialize only the isolated database with the project's existing schema definition (`database/schema.sql`) and the already-approved Node session migration if authentication is required. Do not edit the schema for this test.
4. Create dedicated customer/admin users and one available vehicle fixture using parameterized fixture SQL. Keep all test IDs prefixed with `booking_it_`.

## Test cases

- Create a valid booking and verify the persisted row and `CONFIRMED` status.
- Verify malformed dates, impossible calendar dates, year `0000`, reversed dates, and inclusive overlap are rejected without extra rows.
- Verify `PENDING` and `CONFIRMED` rows block overlap while `CANCELLED` rows do not.
- Verify customer list and cancellation ownership, administrator list/cancellation, and customer create authorization.
- Open two independent API requests (therefore two pool clients/transactions) for the same vehicle and overlapping inclusive dates at a barrier. Assert exactly one request returns 201, the other returns 409, and exactly one corresponding booking row exists.
- Force an insert failure after the overlap query in a disposable transaction and verify rollback leaves no booking row.

## Cleanup safeguards

Cleanup must run only after a fresh `SELECT current_database()` check equals `vrms_node_booking_test`. Delete only rows with the test ID prefix and only from that database. Never use `DROP`, `TRUNCATE`, or an unqualified cleanup command. The database may be removed later only through a separately approved test-environment cleanup procedure.

The current automated concurrency test uses the in-memory fake PostgreSQL implementation and is not a substitute for this real PostgreSQL transaction/locking test.
