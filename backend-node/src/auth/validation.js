import { z } from 'zod';

function matchesExistingEmailRule(email) {
  const at = email.indexOf('@');
  const dot = email.indexOf('.', at === -1 ? 0 : at);
  return at !== -1 && dot !== -1 && at > 0 && dot > at + 1 && dot < email.length - 1;
}

export const registrationSchema = z.object({
  userId: z.string().min(1),
  name: z.string().min(1),
  email: z.string().refine(matchesExistingEmailRule),
  password: z.string().refine((value) => Buffer.byteLength(value, 'utf8') >= 8),
});

export function parseRegistration(input) {
  return registrationSchema.safeParse(input);
}

export function isValidLoginInput(email, password) {
  return typeof email === 'string' && matchesExistingEmailRule(email) &&
    typeof password === 'string' && password.length > 0;
}
