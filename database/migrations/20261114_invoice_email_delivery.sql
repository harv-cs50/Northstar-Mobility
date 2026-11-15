-- Adds email delivery state and one-invoice-per-booking protection.
-- Apply only after backing up the database. No invoice or payment rows are deleted.
-- Existing duplicate invoices are preserved; the migration stops before changes
-- so they can be reviewed rather than discarded automatically.

BEGIN;

DO $$
BEGIN
    IF EXISTS (
        SELECT booking_id
        FROM invoices
        GROUP BY booking_id
        HAVING COUNT(*) > 1
    ) THEN
        RAISE EXCEPTION
            'Invoice email migration stopped: duplicate invoices per booking exist; review them without deleting records';
    END IF;
END
$$;

ALTER TABLE invoices
    ADD COLUMN IF NOT EXISTS email_status varchar NOT NULL DEFAULT 'PENDING';
ALTER TABLE invoices
    ADD COLUMN IF NOT EXISTS email_attempts integer NOT NULL DEFAULT 0;
ALTER TABLE invoices
    ADD COLUMN IF NOT EXISTS email_last_attempt_at timestamp with time zone;
ALTER TABLE invoices
    ADD COLUMN IF NOT EXISTS email_sent_at timestamp with time zone;
ALTER TABLE invoices
    ADD COLUMN IF NOT EXISTS email_last_error text;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'invoices_email_status_check'
          AND conrelid = 'invoices'::regclass
    ) THEN
        ALTER TABLE invoices ADD CONSTRAINT invoices_email_status_check
            CHECK (email_status IN ('PENDING', 'SENDING', 'SENT', 'FAILED'));
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'invoices_email_attempts_check'
          AND conrelid = 'invoices'::regclass
    ) THEN
        ALTER TABLE invoices ADD CONSTRAINT invoices_email_attempts_check
            CHECK (email_attempts >= 0);
    END IF;
END
$$;

CREATE UNIQUE INDEX IF NOT EXISTS ux_invoices_booking_id
    ON invoices (booking_id);
CREATE INDEX IF NOT EXISTS idx_invoices_email_status
    ON invoices (email_status, email_last_attempt_at);

COMMIT;
