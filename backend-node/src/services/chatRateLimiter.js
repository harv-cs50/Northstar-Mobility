/**
 * In-memory sliding-window implementation. Chat routes depend only on allow(key),
 * so a shared-store implementation can be injected when the API is scaled out.
 */
export class InMemoryChatRateLimiter {
  constructor({ limit = 12, windowMs = 60_000, now = () => Date.now() } = {}) {
    if (!Number.isInteger(limit) || limit < 1 || !Number.isFinite(windowMs) || windowMs < 1) {
      throw new TypeError('Chat rate limit and window must be positive');
    }
    this.limit = limit;
    this.windowMs = windowMs;
    this.now = now;
    this.requests = new Map();
  }

  async allow(key) {
    const now = this.now();
    const windowStart = now - this.windowMs;

    // Opportunistically discard expired principals so the map stays bounded over time.
    for (const [storedKey, timestamps] of this.requests) {
      const active = timestamps.filter((timestamp) => timestamp > windowStart);
      if (active.length === 0) this.requests.delete(storedKey);
      else if (active.length !== timestamps.length) this.requests.set(storedKey, active);
    }

    const active = this.requests.get(key) ?? [];
    if (active.length >= this.limit) return false;
    active.push(now);
    this.requests.set(key, active);
    return true;
  }
}
