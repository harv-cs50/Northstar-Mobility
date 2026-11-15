import { z } from 'zod';

const exactOrigin = z.string().url().refine((value) => {
  try {
    const parsed = new URL(value);
    return (parsed.protocol === 'http:' || parsed.protocol === 'https:') &&
      parsed.origin === value;
  } catch {
    return false;
  }
}, 'must be one exact HTTP or HTTPS origin without a path or trailing slash');

const environmentSchema = z.object({
  PGHOST: z.string().trim().min(1, 'is required'),
  PGPORT: z.coerce.number().int().min(1).max(65535),
  PGDATABASE: z.string().trim().min(1, 'is required'),
  PGUSER: z.string().trim().min(1, 'is required'),
  PGPASSWORD: z.string().min(1, 'is required'),
  ALLOWED_ORIGIN: exactOrigin,
  VRMS_ADMIN_BOOTSTRAP_TOKEN: z.string().optional().default(''),
  VRMS_INVOICE_TAX_PERCENT: z.preprocess(
    (value) => value === undefined || value === '' ? 0 : value,
    z.coerce.number().finite().min(0).max(100),
  ).default(0),
  PORT: z.preprocess(
    (value) => value === undefined || value === '' ? 8080 : value,
    z.coerce.number().int().min(1).max(65535),
  ),
  NODE_ENV: z.enum(['development', 'test', 'production']).default('development'),
});

export function loadConfig(environment = process.env) {
  const result = environmentSchema.safeParse(environment);
  if (!result.success) {
    const details = result.error.issues
      .map((issue) => `${issue.path.join('.') || 'environment'}: ${issue.message}`)
      .join('; ');
    // Report variable names and validation messages only; never echo environment values.
    throw new Error(`Invalid environment configuration: ${details}`);
  }

  const values = result.data;
  return {
    host: values.PGHOST,
    port: values.PORT,
    nodeEnv: values.NODE_ENV,
    allowedOrigin: values.ALLOWED_ORIGIN,
    adminBootstrapToken: values.VRMS_ADMIN_BOOTSTRAP_TOKEN,
    invoiceTaxPercent: values.VRMS_INVOICE_TAX_PERCENT,
    database: {
  host: values.PGHOST,
  port: values.PGPORT,
  database: values.PGDATABASE,
  user: values.PGUSER,
  password: values.PGPASSWORD,
  ssl: values.NODE_ENV === 'production'
    ? { rejectUnauthorized: true }
    : false,
  application_name: 'northstar-vrms-api',
  max: 10,
},
  };
}
