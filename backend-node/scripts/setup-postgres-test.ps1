$ErrorActionPreference = 'Stop'

try {
  $backendNodeDirectory = Split-Path -Parent $PSScriptRoot
  $projectDirectory = Split-Path -Parent $backendNodeDirectory
  $schemaPath = Join-Path $projectDirectory 'database\schema.sql'
  $sessionMigrationPath = Join-Path $backendNodeDirectory 'database\migrations\001_auth_sessions.sql'
  $testEnvironmentFile = Join-Path $backendNodeDirectory '.env.test'

  if (Test-Path -LiteralPath $testEnvironmentFile) {
    foreach ($line in Get-Content -LiteralPath $testEnvironmentFile) {
      if ($line -match '^\s*(#|$)') { continue }
      $parts = $line -split '=', 2
      if ($parts.Count -ne 2) { continue }
      $key = $parts[0].Trim()
      if ($key -notin @('VRMS_TEST_PGUSER', 'VRMS_TEST_PGPASSWORD')) { continue }
      if (Test-Path "Env:$key") { continue }
      $value = $parts[1].Trim().Trim('"').Trim("'")
      Set-Item "Env:$key" $value
    }
  }

  if (-not $env:VRMS_TEST_PGUSER) {
    throw 'Set VRMS_TEST_PGUSER in this PowerShell process or in the ignored backend-node/.env.test file.'
  }
  if (-not $env:VRMS_TEST_PGPASSWORD) {
    throw 'Set VRMS_TEST_PGPASSWORD in this PowerShell process or in the ignored backend-node/.env.test file.'
  }

  # Use only the dedicated test password; never inspect or preserve inherited PGPASSWORD.
  $env:PGPASSWORD = $env:VRMS_TEST_PGPASSWORD

  # This script has no database-name parameter. It creates only vrms_node_test
  # and explicitly uses postgres as the maintenance database.
  & createdb.exe --no-password --host=127.0.0.1 --port=5432 `
    --username=$env:VRMS_TEST_PGUSER --maintenance-db=postgres vrms_node_test
  if ($LASTEXITCODE -ne 0) {
    throw 'Could not create vrms_node_test. No existing database was dropped or overwritten; stop if the database already exists.'
  }

  $actualDatabase = (& psql.exe --no-password --no-psqlrc --tuples-only --no-align `
    --host=127.0.0.1 --port=5432 --username=$env:VRMS_TEST_PGUSER `
    --dbname=vrms_node_test --command='SELECT current_database();').Trim()
  if ($LASTEXITCODE -ne 0 -or $actualDatabase -cne 'vrms_node_test') {
    throw 'Refusing schema setup because current_database() did not equal vrms_node_test.'
  }

  & psql.exe --no-password --no-psqlrc --set=ON_ERROR_STOP=1 `
    --host=127.0.0.1 --port=5432 --username=$env:VRMS_TEST_PGUSER `
    --dbname=vrms_node_test --file=$schemaPath
  if ($LASTEXITCODE -ne 0) { throw 'The project fresh-database schema failed on vrms_node_test.' }

  $actualDatabase = (& psql.exe --no-password --no-psqlrc --tuples-only --no-align `
    --host=127.0.0.1 --port=5432 --username=$env:VRMS_TEST_PGUSER `
    --dbname=vrms_node_test --command='SELECT current_database();').Trim()
  if ($LASTEXITCODE -ne 0 -or $actualDatabase -cne 'vrms_node_test') {
    throw 'Refusing migration because current_database() did not equal vrms_node_test.'
  }

  & psql.exe --no-password --no-psqlrc --set=ON_ERROR_STOP=1 `
    --host=127.0.0.1 --port=5432 --username=$env:VRMS_TEST_PGUSER `
    --dbname=vrms_node_test --file=$sessionMigrationPath
  if ($LASTEXITCODE -ne 0) { throw 'The auth_sessions migration failed on vrms_node_test.' }

  $actualDatabase = (& psql.exe --no-password --no-psqlrc --tuples-only --no-align `
    --host=127.0.0.1 --port=5432 --username=$env:VRMS_TEST_PGUSER `
    --dbname=vrms_node_test --command='SELECT current_database();').Trim()
  if ($LASTEXITCODE -ne 0 -or $actualDatabase -cne 'vrms_node_test') {
    throw 'Final database-name verification failed.'
  }
  Write-Output 'Created vrms_node_test and applied the fresh schema plus auth_sessions migration.'
}
finally {
  Remove-Item Env:PGPASSWORD -ErrorAction SilentlyContinue
}
