-- Add two sample fleet vehicles without updating or deleting existing records.
-- A conflicting ID with different details stops and rolls back the transaction.
BEGIN;

INSERT INTO vehicles (vehicle_id, brand, model, vehicle_type, daily_rate, is_available)
VALUES
    ('DEMO-CRETA-24', 'Hyundai', 'Creta 1.5 Plus 2024', 'SUV', 2500.00, TRUE),
    ('DEMO-SWIFT-24', 'Suzuki', 'Swift 2024', 'Car', 1500.00, TRUE)
ON CONFLICT (vehicle_id) DO NOTHING;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM vehicles
        WHERE vehicle_id = 'DEMO-CRETA-24'
          AND brand = 'Hyundai'
          AND model = 'Creta 1.5 Plus 2024'
          AND vehicle_type = 'SUV'
          AND daily_rate = 2500.00
    ) THEN
        RAISE EXCEPTION 'Vehicle ID DEMO-CRETA-24 already belongs to a different record';
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM vehicles
        WHERE vehicle_id = 'DEMO-SWIFT-24'
          AND brand = 'Suzuki'
          AND model = 'Swift 2024'
          AND vehicle_type = 'Car'
          AND daily_rate = 1500.00
    ) THEN
        RAISE EXCEPTION 'Vehicle ID DEMO-SWIFT-24 already belongs to a different record';
    END IF;
END
$$;

COMMIT;
