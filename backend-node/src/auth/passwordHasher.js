import argon2 from 'argon2';

const cppCompatibleOptions = {
  type: argon2.argon2id,
  timeCost: 3,
  memoryCost: 65_536,
  parallelism: 1,
  hashLength: 32,
  saltLength: 16,
};

export function hashPassword(password) {
  return argon2.hash(password, cppCompatibleOptions);
}

export async function verifyPassword(password, encodedHash) {
  if (typeof password !== 'string' || !password ||
      typeof encodedHash !== 'string' || !encodedHash) return false;
  try {
    return await argon2.verify(encodedHash, password);
  } catch {
    return false;
  }
}
