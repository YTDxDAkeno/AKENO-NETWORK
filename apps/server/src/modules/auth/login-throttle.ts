/**
 * In-memory failed-login tracking for brute-force protection on a single backend instance.
 *
 * - Per account: after 5 failures within 15 minutes, further attempts are delayed with an
 *   exponential lockout (30 s, 60 s, 120 s, ... capped at 15 minutes).
 * - Per IP address: after 30 failures within 15 minutes the address is locked for 15 minutes.
 *
 * Successful logins clear the account counter. State resets on restart, which is acceptable
 * because the HTTP rate limiter and Argon2 cost still bound guessing speed.
 */
interface Bucket {
  failures: number;
  firstFailureAt: number;
  lockedUntil: number;
}

const WINDOW_MS = 15 * 60 * 1000;
const ACCOUNT_FREE_ATTEMPTS = 5;
const IP_FREE_ATTEMPTS = 30;
const MAX_LOCK_MS = 15 * 60 * 1000;

export class LoginThrottle {
  private readonly accounts = new Map<string, Bucket>();
  private readonly addresses = new Map<string, Bucket>();

  /** Returns the number of milliseconds the caller must wait, or 0 if an attempt is allowed. */
  retryAfterMs(accountKey: string, ip: string, now = Date.now()): number {
    const account = this.current(this.accounts, accountKey, now);
    const address = this.current(this.addresses, ip, now);
    return Math.max(0, (account?.lockedUntil ?? 0) - now, (address?.lockedUntil ?? 0) - now);
  }

  recordFailure(accountKey: string, ip: string, now = Date.now()): void {
    const account = this.bump(this.accounts, accountKey, now);
    if (account.failures > ACCOUNT_FREE_ATTEMPTS) {
      const exponent = account.failures - ACCOUNT_FREE_ATTEMPTS - 1;
      account.lockedUntil = now + Math.min(MAX_LOCK_MS, 30_000 * 2 ** exponent);
    }
    const address = this.bump(this.addresses, ip, now);
    if (address.failures > IP_FREE_ATTEMPTS) address.lockedUntil = now + MAX_LOCK_MS;
  }

  recordSuccess(accountKey: string): void {
    this.accounts.delete(accountKey);
  }

  /** Drops expired buckets; called periodically. */
  prune(now = Date.now()): void {
    for (const map of [this.accounts, this.addresses]) {
      for (const [key, bucket] of map) {
        if (now - bucket.firstFailureAt > WINDOW_MS && bucket.lockedUntil < now) map.delete(key);
      }
    }
  }

  private current(map: Map<string, Bucket>, key: string, now: number): Bucket | undefined {
    const bucket = map.get(key);
    if (bucket && now - bucket.firstFailureAt > WINDOW_MS && bucket.lockedUntil < now) {
      map.delete(key);
      return undefined;
    }
    return bucket;
  }

  private bump(map: Map<string, Bucket>, key: string, now: number): Bucket {
    let bucket = this.current(map, key, now);
    if (!bucket) {
      bucket = { failures: 0, firstFailureAt: now, lockedUntil: 0 };
      map.set(key, bucket);
    }
    bucket.failures += 1;
    return bucket;
  }
}
