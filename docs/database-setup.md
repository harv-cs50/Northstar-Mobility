# PostgreSQL connection and fresh database setup

The backend reads PostgreSQL settings from the standard libpq environment
variables. Set all five variables before starting the application:

| Variable | Purpose | Typical local value |
| --- | --- | --- |
| `PGHOST` | PostgreSQL server | `127.0.0.1` |
| `PGPORT` | PostgreSQL port | `5432` |
| `PGDATABASE` | Database name | `vrms_demo` (new empty database) |
| `PGUSER` | PostgreSQL role | Your local database role |
| `PGPASSWORD` | Password for that role | Your local database password |

If a variable is missing or empty, the backend reports its name and the
variables that need to be set. No connection settings are loaded from source
code or from a `.env` file.

For a fresh installation, create a separate empty database and apply
[`../database/schema.sql`](../database/schema.sql). Do not run that bootstrap
file against a populated database; it is not a migration and does not reconcile
existing schema differences. The repository root `README.md` contains the full
application setup and run instructions.

## PowerShell (VS Code terminal)

Set the non-secret values in the terminal session:

```powershell
$env:PGHOST = "127.0.0.1"
$env:PGPORT = "5432"
$env:PGDATABASE = "vrms_demo"
$env:PGUSER = "your_postgres_role"
```

Read the password without echoing or adding it to shell history:

```powershell
$securePassword = Read-Host "PostgreSQL password" -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
try {
    $env:PGPASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
}
finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
}
```

The application and child processes launched from this terminal inherit these
settings. Close the terminal or remove the variables when finished.

## MSYS2 UCRT64 terminal

```bash
export PGHOST=127.0.0.1
export PGPORT=5432
export PGDATABASE=vrms_demo
export PGUSER=your_postgres_role
read -rsp 'PostgreSQL password: ' PGPASSWORD
printf '\n'
export PGPASSWORD
```

## Build and run

From the repository root in PowerShell:

```powershell
cmake -S backend -B backend/build -G Ninja
cmake --build backend/build
ctest --test-dir backend/build --output-on-failure
backend\build\VRMSApi.exe
```

The local `.env` and `.env.*` patterns are ignored by Git. Do not put real
passwords in source files, documentation, or committed environment files.
