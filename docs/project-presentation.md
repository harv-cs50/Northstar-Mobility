# VRMS project presentation outline

## 1. Problem and goal

Rental teams need one place to publish fleet availability, manage reservations,
record rental and payment activity, and review maintenance. Customers need to
find vehicles and keep their booking and invoice details together.

## 2. Architecture

- React and Tailwind customer site and administrator dashboard.
- C++20 REST API with session and role checks.
- Existing C++ models, services, and repositories for core business logic.
- PostgreSQL persistence for accounts, fleet, bookings, rentals, simulated
  payments, invoices, audit events, and maintenance records.

## 3. Security decisions

- Argon2id password hashes; raw passwords are not stored.
- Customer registration assigns the role on the server.
- First-admin provisioning uses a one-time environment token and PostgreSQL
  transaction-scoped advisory lock.
- Later admin provisioning requires an authenticated admin session and a role
  recheck against PostgreSQL.
- Customer booking and payment ownership comes from the session.
- API binds to loopback by default for local demonstration.

## 4. Demonstration path

1. Create a new VRMS database and load `database/schema.sql`.
2. Start the C++ API with PostgreSQL environment variables and a random
   `VRMS_ADMIN_BOOTSTRAP_TOKEN`.
3. Provision the first admin, then add a vehicle in the admin workspace.
4. Register as a customer, search the fleet, and make a booking.
5. Demonstrate an overlapping booking rejection.
6. Run the simulated checkout and open the generated invoice.
7. Inspect bookings, fleet, rentals, and maintenance overview as admin.

## 5. Scope and next steps

The payment flow is a simulation. Sessions are process-local; the API uses HTTP
and is loopback-only; no external deployment is configured. Production follow-up
includes TLS behind a reverse proxy, durable sessions, rate limiting, monitoring,
and data-preserving migrations for existing databases.
