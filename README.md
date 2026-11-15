# Vehicle Rental Management System (VRMS)

VRMS is a Windows/MSYS2 C++20 and PostgreSQL vehicle rental application with a React and Tailwind customer site and administrator dashboard. The REST API wraps the existing repositories, services, and models; application records are persisted in PostgreSQL.

## Implemented features

- Argon2id customer registration and login, expiring in-memory sessions, and role checks.
- One-time token-protected initial administrator setup and authenticated admin provisioning.
- Available vehicle browsing and search.
- Customer booking creation, history, and cancellation. Booking dates are validated and overlapping reservations are checked while a PostgreSQL vehicle row is locked.
- Admin fleet create/update, booking management, maintenance and rental overviews, and administrator provisioning.
- Demonstration payment processing using existing payment strategies. Simulated payment and invoice records are committed together; no real payment provider or card data is involved.
- Idempotent invoice creation, professionally formatted PDF invoices, and SMTP delivery to the registered customer email. Email delivery status is visible in customer history and administrators can retry failed/pending messages.
- Customer payment and invoice history.
- Read-only Gemini customer support with live fleet information, server-calculated estimates, and session-scoped booking, payment, and invoice email status for signed-in customers.
- Fresh-database schema and environment-based configuration.

The web/API surface is for a local project demonstration. The API binds to loopback by default, uses plain HTTP, and stores sessions in process memory. Do not expose it publicly as-is. A production deployment needs TLS/reverse proxying, durable sessions, rate limits, monitoring, and a managed secret store.

## Requirements

- Windows with MSYS2 UCRT64 and CMake, Ninja, GCC, libpqxx 8.x, PostgreSQL client libraries, Argon2, libcurl, libHaru, and BCrypt.
- PostgreSQL 18 (or compatible).
- Node.js and npm. Frontend dependency versions are pinned in `frontend/package.json`.

## Database setup

Create a **new, empty** database for VRMS. Do not run the schema file against a database containing data or an existing production database. `database/schema.sql` is a fresh-install bootstrap, not a migration and does not reconcile existing schemas.

In a VS Code PowerShell terminal, set the connection variables. Keep secrets in the terminal environment; do not put them in source or commit a local `.env` file.

```powershell
$env:PGHOST = "127.0.0.1"
$env:PGPORT = "5432"
$env:PGDATABASE = "vrms_demo"
$env:PGUSER = "your_postgres_role"
$securePassword = Read-Host "PostgreSQL password" -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
try {
    $env:PGPASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
}
```

Create that database once, then load the schema:

```powershell
createdb vrms_demo
psql -v ON_ERROR_STOP=1 -d vrms_demo -f database/schema.sql
```

The schema matches the current C++ persistence queries. Existing installations may also contain legacy/planned `maintenance`, `coupons`, and `reviews` tables; the current backend does not query those tables (`maintenance_records` is the implemented maintenance table). This schema creates the tables used by the current implementation. Review an existing database and use a data-preserving migration before changing it.

The fresh-install schema includes two sample vehicles: a Hyundai Creta 1.5 Plus 2024 (₹2,500/day) and Suzuki Swift 2024 (₹1,500/day). To add them to an existing database, back it up, verify the target database, then run `psql -v ON_ERROR_STOP=1 -d $env:PGDATABASE -f database/migrations/20261115_add_two_demo_vehicles.sql`. This migration inserts only those two IDs; it never updates or deletes existing vehicle rows and aborts if an ID is already used for different vehicle details. The customer fleet cards use exact-model photos hosted on Wikimedia Commons and include attribution and license links.

## Build and run the C++ backend

Install the extra UCRT64 PDF and SMTP libraries if they are not already present:

```bash
pacman -S mingw-w64-ucrt-x86_64-libharu mingw-w64-ucrt-x86_64-curl-winssl
```

From the repository root in MSYS2 UCRT64 (or PowerShell with the toolchain on `PATH`):

```powershell
cmake -S backend -B backend/build -G Ninja
cmake --build backend/build
ctest --test-dir backend/build --output-on-failure
```

Start the REST API from the same terminal that has the `PG*` settings:

```powershell
$env:VRMS_API_HOST = "127.0.0.1" # loopback is the default
$env:VRMS_API_PORT = "8080"
backend\build\VRMSApi.exe
```

Open `http://127.0.0.1:8080/api/health`; a healthy response includes the connected database name. The existing console demonstration remains available as `backend\build\VehicleRentalSystem.exe`.

## First administrator setup

Generate a random bootstrap token locally. Do not share or commit it:

```powershell
$tokenBytes = [byte[]]::new(32)
[Security.Cryptography.RandomNumberGenerator]::Fill($tokenBytes)
$env:VRMS_ADMIN_BOOTSTRAP_TOKEN = [Convert]::ToHexString($tokenBytes)
```

Start `VRMSApi.exe` from this terminal, open the frontend, and choose **First-time administrator setup**. The token is sent in a request header and is never stored in PostgreSQL or returned by the API. PostgreSQL transaction-scoped advisory locking ensures concurrent bootstrap requests cannot create multiple first administrators. Once an admin exists, bootstrap is rejected; additional admins require a logged-in administrator session. Customer registration always assigns `CUSTOMER`; submitted role/access-level fields are ignored. The current schema persists only the `ADMIN`/`CUSTOMER` role, so no admin access level is accepted from the client.

## Start the React app

In another terminal:

```powershell
cd frontend
npm install
npm run dev
```

Open `http://127.0.0.1:5173`. Vite proxies `/api` to `http://127.0.0.1:8080`. Set `VRMS_ALLOWED_ORIGIN` on the API only if you change the frontend origin. A static deployment can set `VITE_API_BASE_URL` at build time to the API's `/api` URL and configure a matching `VRMS_ALLOWED_ORIGIN`; use HTTPS and a reverse proxy before public deployment. Create a static bundle using `npm run build`; output is written to ignored `frontend/dist/`.

## Gemini customer support setup

The floating Northstar Assist chat calls Google's Gemini API from the C++ backend. The browser sends chat text only to `/api/chat`; it never receives the Gemini key. Set `GEMINI_API_KEY` to the complete key from Google AI Studio, including an `AQ.` prefix if present; the backend treats it as an opaque value and sends it unchanged in Google's documented `x-goog-api-key` header. Do not add a `Bearer` prefix or put the key in the URL. This API-key header is the documented REST authentication method for both standard and authorization keys. The backend uses `POST https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent`; GenerateContent remains supported, although Google now recommends the Interactions API for new projects. `.env.example` uses `gemini-3.8-flash`, listed as a stable model in Google's [Gemini model guide](https://ai.google.dev/gemini-api/docs/models). Model availability and account quotas can change; quota exhaustion is reported as a retryable chat error. See Google's [API key authentication guide](https://ai.google.dev/gemini-api/docs/api-key) and [Gemini API reference](https://ai.google.dev/api) for current key requirements.

Set the model and enter the key without echoing it in the VS Code PowerShell terminal that will start `VRMSApi.exe`:

```powershell
$env:GEMINI_MODEL = "gemini-3.8-flash"
$secureGeminiKey = Read-Host "Gemini API key" -AsSecureString
$geminiKeyPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureGeminiKey)
try {
    $env:GEMINI_API_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($geminiKeyPointer)
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($geminiKeyPointer)
}
```

Start the API from that same terminal. Keep the key out of source files, Git, frontend settings, browser requests, and logs. `.env.example` is a placeholder reference only; this application does not load `.env` files automatically. Booking, payment, and invoice lookups are answered by the backend from customer-scoped repository queries; those private records and the authenticated user ID are never sent to Gemini. Only public fleet context and the customer's general message/history are sent for open-ended questions; common email, credential, Gemini-key, and payment-number patterns are redacted before the request leaves the backend. The context excludes the customer's name, email, password, and session token. Anonymous visitors receive public fleet information only. Gemini free-tier quotas and model access are controlled by Google and vary by account; customers may see a temporary retry message when a quota or rate limit is reached.

Chat is read-only. It does not create or cancel bookings or process payments. It allows at most 12 messages per minute per authenticated account or client IP, limits each message to 1,000 UTF-8 bytes, and limits recent conversation history to 4,000 UTF-8 bytes. The API loads fleet and customer data through parameterized repository methods; it does not allow Gemini to run SQL or call application tools.

## Deployment preparation

The frontend API URL is build-time configurable through `VITE_API_BASE_URL`. The default `/api` supports a same-origin reverse proxy. The only hardcoded `127.0.0.1:8080` URL is Vite's development proxy in `frontend/vite.config.js`; it is not included as the production API URL in the built app.

The API CORS policy permits one exact origin from `VRMS_ALLOWED_ORIGIN` (the local default is `http://localhost:5173`). For a separately hosted frontend, set that variable to the full HTTPS frontend origin, such as `https://rentals.example.com`, and build the frontend with `VITE_API_BASE_URL=https://api.example.com/api`. Do not use `*`. For a same-origin setup, have the reverse proxy send `/api/*` to the API and serve the static frontend for other paths; this avoids cross-origin requests.

### Production build

The current C++ API is Windows-specific. Build it on Windows/MSYS2 UCRT64 in Release mode:

```powershell
cmake -S backend -B backend/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build-release
ctest --test-dir backend/build-release --output-on-failure
```

The API executable needs the UCRT64 runtime DLLs and the runtime libraries for libpqxx/libpq, libcurl, libHaru, and Argon2. Keep the matching MSYS2 UCRT64 runtime available on `PATH`, or package the required DLLs alongside the executable. Windows provides Winsock and BCrypt system libraries. The PostgreSQL server itself can be managed separately.

Build the static React bundle. Set only a public URL here; Vite embeds `VITE_*` values into client files:

```powershell
cd frontend
npm ci
$env:VITE_API_BASE_URL = "/api"
npm run build
```

The output is `frontend/dist/`. Serve it from a static web server or reverse proxy with SPA fallback to `index.html`. Do not run `vite preview` as a production web server.

### Production start

On a Windows host with the required MSYS2 runtime dependencies installed, configure the database and SMTP variables in the API process environment as described below. For a reverse proxy on the same machine, keep the API bound to loopback:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$env:VRMS_API_HOST = "127.0.0.1"
$env:VRMS_API_PORT = "8080"
$env:VRMS_ALLOWED_ORIGIN = "https://rentals.example.com"
backend\build-release\VRMSApi.exe
```

Terminate the process with the host's service manager, keep port 8080 private, and allow public traffic only through the TLS reverse proxy on ports 80/443. The API speaks plain HTTP and does not serve the React files itself. Caddy can serve static files and reverse proxy `/api/*`; see its [static-site and reverse-proxy patterns](https://caddyserver.com/docs/caddyfile/patterns) and [Windows service instructions](https://caddyserver.com/docs/running).

For separate frontend/API hosts, set the API host to the private interface reachable by the proxy and set `VRMS_ALLOWED_ORIGIN` to exactly the frontend origin. The API currently supports only one allowed origin per process.

### Environment-variable reference

Set backend secrets using the host's secret manager or process environment. The application does not load `.env` files. The frontend build receives only `VITE_API_BASE_URL`; any `VITE_*` value is public and must never contain secrets.

| Variable | Required | Use |
| --- | --- | --- |
| `PGHOST` | Yes | PostgreSQL hostname |
| `PGPORT` | Yes | PostgreSQL port |
| `PGDATABASE` | Yes | Application database |
| `PGUSER` | Yes | Least-privilege database role |
| `PGPASSWORD` | Yes | Database password; keep in secret storage |
| `PGSSLMODE` | Recommended for remote DB | Standard libpq TLS mode; use provider requirements, usually `verify-full` with a trusted root certificate |
| `PGSSLROOTCERT` | If required by provider | Path to the trusted PostgreSQL server CA certificate |
| `VRMS_API_HOST` | No | IPv4 bind address; defaults to loopback `127.0.0.1` |
| `VRMS_API_PORT` | No | Listen port; defaults to `8080` |
| `VRMS_ALLOWED_ORIGIN` | No | One exact allowed browser origin; defaults to local Vite origin |
| `VRMS_ADMIN_BOOTSTRAP_TOKEN` | First admin only | One-time bootstrap secret; do not put in a `VITE_*` variable, and unset after provisioning |
| `VRMS_SMTP_HOST` | Email required | SMTP hostname |
| `VRMS_SMTP_PORT` | Email required | SMTP port, normally 587 or 465 |
| `VRMS_SMTP_USERNAME` | Email required | SMTP username |
| `VRMS_SMTP_PASSWORD` | Email required | SMTP secret/app password |
| `VRMS_SMTP_FROM_EMAIL` | Email required | Authorized sender address |
| `VRMS_SMTP_FROM_NAME` | Email required | Sender display name |
| `VRMS_SMTP_SECURITY` | No | `starttls` (default), `ssl`, or `none` |
| `VRMS_SMTP_TIMEOUT_SECONDS` | No | SMTP timeout, default `20` |
| `VRMS_INVOICE_TAX_PERCENT` | No | Tax percent for new invoices; default `0` |
| `GEMINI_API_KEY` | Chat required | Google AI Studio API key; backend process only, never expose through `VITE_*` |
| `GEMINI_MODEL` | Chat required | Supported Gemini model ID, e.g. `gemini-3.8-flash` |
| `VITE_API_BASE_URL` | Frontend build | Public API base URL; default `/api`, compiled into frontend assets |

### Deployment checklist

- [ ] Choose a Windows host for the current API, or port the socket and secure-random code before using Linux. The API includes Winsock headers/calls; password salts and session IDs use Windows BCrypt; CMake links `ws2_32`/`bcrypt` and contains MSYS2-specific library hints. The current backend cannot build on Linux as-is.
- [ ] Create a new production database, or back up and review an existing database before applying migrations. For a fresh database, run `database/schema.sql` only; it creates the invoice email columns and matching named constraints/indexes. `database/migrations/20261114_invoice_email_delivery.sql` is an upgrade migration for an older populated schema and assumes the `invoices` table already exists. Do not run both against a populated database without reviewing its state.
- [ ] Use a least-privilege PostgreSQL role, require TLS for remote database connections, and keep the database port private.
- [ ] Configure all required DB and SMTP variables in the backend host's secret store. Use a valid sender and test invoice delivery before public access.
- [ ] Build the frontend with the intended API URL. Verify the generated bundle contains no secrets; only `VITE_API_BASE_URL` belongs there.
- [ ] Configure a TLS reverse proxy, SPA fallback, and exact CORS origin. Do not expose the API's plain-HTTP port directly to the Internet.
- [ ] Generate a high-entropy bootstrap token for initial setup, use HTTPS, provision the first admin, then remove the token from the runtime environment.
- [ ] The bootstrap token is not compiled into the frontend, but the current initial-admin screen asks the operator to enter it and sends it in an HTTPS request header. If the token must never enter a browser, replace this bootstrap flow with out-of-band provisioning before public launch.
- [ ] Keep one API instance: sessions are process-local memory, so restarts sign users out and multiple replicas will not share sessions.
- [ ] Confirm backup/restore, application logs, monitoring, and an SMTP failure/retry procedure. The repository ignores local `.env` files and database backup artifacts; verify `git status` before publishing.

This setup is suitable for a controlled demonstration after the checklist is complete, but it is not production-hardened: the API has no built-in TLS or durable/shared sessions. The chatbot has a basic per-account/client-IP request limit; other API routes do not have general rate limiting. The custom HTTP server should remain behind a maintained reverse proxy until those controls are addressed.

## Invoice email configuration

The API sends an invoice PDF after a simulated payment succeeds. Payments remain simulated: the application does not collect or send card details to a payment provider. SMTP is configured through the API process environment; `.env.example` lists every variable with placeholders, and the application does not load `.env` files automatically. Set these in the same terminal that starts `VRMSApi.exe`:

| Variable | Required | Meaning |
| --- | --- | --- |
| `VRMS_SMTP_HOST` | Yes | SMTP host name or IP address |
| `VRMS_SMTP_PORT` | Yes | SMTP port, commonly `587` for STARTTLS or `465` for implicit TLS |
| `VRMS_SMTP_USERNAME` | Yes | SMTP account username |
| `VRMS_SMTP_PASSWORD` | Yes | SMTP account password or provider app password; keep it out of source control |
| `VRMS_SMTP_FROM_EMAIL` | Yes | Sender email address authorized by the SMTP provider |
| `VRMS_SMTP_FROM_NAME` | Yes | Sender display name |
| `VRMS_SMTP_SECURITY` | No | `starttls` (default), `ssl`, or `none` |
| `VRMS_SMTP_TIMEOUT_SECONDS` | No | Network timeout from 1 to 300 seconds; default `20` |
| `VRMS_INVOICE_TAX_PERCENT` | No | Tax percentage added to new simulated payments; default `0` |

Use a provider-issued app password where supported. Do not put actual credentials in `.env.example`, source, or committed files. For a local PowerShell session, set non-secret values directly and prompt for the SMTP password without echoing it:

```powershell
$env:VRMS_SMTP_HOST = "smtp.example.com"
$env:VRMS_SMTP_PORT = "587"
$env:VRMS_SMTP_USERNAME = "your-smtp-username"
$env:VRMS_SMTP_FROM_EMAIL = "invoices@example.com"
$env:VRMS_SMTP_FROM_NAME = "Northstar Mobility"
$env:VRMS_SMTP_SECURITY = "starttls"
$secureSmtpPassword = Read-Host "SMTP app password" -AsSecureString
$smtpPasswordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureSmtpPassword)
try { $env:VRMS_SMTP_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($smtpPasswordPointer) }
finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($smtpPasswordPointer) }
```

If SMTP is missing or rejects a message, the successful payment and invoice remain persisted, the response reports `emailStatus: FAILED`, and an admin can open **Invoices** in the dashboard and choose **Retry email**. States `PENDING`, `SENDING`, `SENT`, and `FAILED` prevent concurrent or accidental duplicate sends. A status stuck at `SENDING` means the server accepted or could not conclusively report a send; check the mail provider before any manual resend. Existing populated databases need the data-preserving `database/migrations/20261114_invoice_email_delivery.sql` migration applied once by an operator; the application never runs migrations automatically. The migration aborts if multiple invoices already exist for one booking so those rows can be reviewed without data loss.

## REST API

Protected routes take `Authorization: Bearer <sessionId>`. Customer IDs are derived from the session, not the request body. Admin routes require an admin session and recheck the role against PostgreSQL.

| Method | Path | Access | Purpose |
| --- | --- | --- | --- |
| GET | `/api/health` | public | Checks PostgreSQL connectivity |
| GET | `/api/vehicles` | public | Lists available vehicles |
| POST | `/api/auth/register` | public | Creates a customer account |
| POST | `/api/auth/login` | public | Starts a session |
| POST | `/api/auth/logout` | session | Ends a session |
| POST | `/api/auth/bootstrap-admin` | bootstrap token | One-time initial admin |
| GET | `/api/bookings` | customer/admin | Own history / all bookings |
| POST | `/api/bookings` | customer | Creates a validated booking |
| POST | `/api/bookings/{id}/cancel` | owner/admin | Cancels a booking |
| GET / POST | `/api/payments` | customer/admin / customer | Payment history / simulated checkout |
| GET | `/api/invoices` | customer/admin | Own invoices / all invoices |
| POST | `/api/admin/invoices/{id}/retry-email` | admin | Retries invoice PDF delivery when it is pending or failed |
| GET / POST | `/api/admin/vehicles` | admin | Fleet inventory / create or update |
| GET | `/api/admin/bookings` | admin | Booking management list |
| GET | `/api/admin/maintenance` | admin | Maintenance overview |
| GET | `/api/admin/rentals` | admin | Rental overview |
| POST | `/api/admin/rentals` | admin | Creates a rental record |
| POST | `/api/admin/rentals/{id}/return` | admin | Completes a rental return |
| POST | `/api/admin/users` | admin | Provisions another administrator |

## Limitations and deployment

- Session records are in memory; API restart signs users out.
- Payments are demonstrations only.
- The UI displays maintenance records; scheduling and completion remain in backend services but have no dashboard form yet.
- No cloud credentials, domain, TLS certificate, or deployment target are available in this workspace, so no external deployment was attempted. API defaults to loopback.
- The project has no Linux build path today; see the platform-specific deployment notes above.
- Optional database-backed authentication concurrency test: create a separate empty `vrms_auth_test` database, set `PGDATABASE=vrms_auth_test`, `PGHOST=127.0.0.1`, and `VRMS_ALLOW_AUTH_TEST_DB=1`, then run `backend\build\AuthTests.exe --integration`. It verifies the current database name before creating an isolated test schema and verifies it again before cleanup. It does not target the application database.
- See [`docs/project-presentation.md`](docs/project-presentation.md) for a short project presentation outline.
