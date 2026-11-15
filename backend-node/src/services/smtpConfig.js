const requiredVariables = [
  'VRMS_SMTP_HOST',
  'VRMS_SMTP_PORT',
  'VRMS_SMTP_USERNAME',
  'VRMS_SMTP_PASSWORD',
  'VRMS_SMTP_FROM_EMAIL',
  'VRMS_SMTP_FROM_NAME',
];

function validEmail(value) {
  return typeof value === 'string'
    && !/[\r\n]/.test(value)
    && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value);
}

export function loadSmtpConfig(environment = process.env) {
  for (const variable of requiredVariables) {
    if (typeof environment[variable] !== 'string' || !environment[variable]) {
      throw new Error(`Missing required SMTP environment variable: ${variable}`);
    }
  }

  const host = environment.VRMS_SMTP_HOST;
  if (/[\/\\\r\n]/.test(host)) {
    throw new Error('VRMS_SMTP_HOST must be a host name or IP address');
  }
  const port = Number(environment.VRMS_SMTP_PORT);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error('VRMS_SMTP_PORT must be a number from 1 to 65535');
  }
  if (!validEmail(environment.VRMS_SMTP_FROM_EMAIL)) {
    throw new Error('VRMS_SMTP_FROM_EMAIL must be a valid email address');
  }
  if (!environment.VRMS_SMTP_FROM_NAME || /[\r\n]/.test(environment.VRMS_SMTP_FROM_NAME)) {
    throw new Error('VRMS_SMTP_FROM_NAME cannot be empty or contain line breaks');
  }

  const security = environment.VRMS_SMTP_SECURITY || 'starttls';
  if (!['starttls', 'ssl', 'none'].includes(security)) {
    throw new Error('VRMS_SMTP_SECURITY must be starttls, ssl, or none');
  }
  const timeoutSeconds = Number(environment.VRMS_SMTP_TIMEOUT_SECONDS || '20');
  if (!Number.isInteger(timeoutSeconds) || timeoutSeconds < 1 || timeoutSeconds > 300) {
    throw new Error('VRMS_SMTP_TIMEOUT_SECONDS must be between 1 and 300');
  }

  return {
    host,
    port,
    username: environment.VRMS_SMTP_USERNAME,
    password: environment.VRMS_SMTP_PASSWORD,
    fromEmail: environment.VRMS_SMTP_FROM_EMAIL,
    fromName: environment.VRMS_SMTP_FROM_NAME,
    security,
    timeoutSeconds,
  };
}

export { validEmail as isValidEmail };
