export function isValidEmail(value) {
  return typeof value === 'string'
    && !/[\r\n]/.test(value)
    && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value);
}
