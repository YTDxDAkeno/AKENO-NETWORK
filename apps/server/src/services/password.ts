import { type Algorithm, hash, parseOptions, verify } from '@node-rs/argon2';
import type { AppConfig } from '../config/env.js';

/** @node-rs/argon2 exposes Algorithm as a const enum; 2 is Argon2id. */
const ARGON2ID = 2 as Algorithm;

/**
 * Argon2id password hashing.
 *
 * Defaults follow the OWASP Password Storage Cheat Sheet minimum configuration for Argon2id:
 * m=19456 KiB (19 MiB), t=2, p=1. Parameters are configurable through environment variables
 * and existing hashes are transparently upgraded on the next successful login.
 *
 * A small semaphore bounds concurrent hashing so a burst of logins cannot exhaust memory on a
 * small VPS (each hash allocates `memoryCost` KiB).
 */
export class PasswordService {
  private active = 0;
  private readonly waiters: Array<() => void> = [];
  private dummyHash: Promise<string> | null = null;

  constructor(private readonly options: AppConfig['argon2']) {}

  async hash(password: string): Promise<string> {
    return this.withPermit(() =>
      hash(password, {
        algorithm: ARGON2ID,
        memoryCost: this.options.memoryCost,
        timeCost: this.options.timeCost,
        parallelism: this.options.parallelism,
        outputLen: 32,
      }),
    );
  }

  async verify(passwordHash: string, password: string): Promise<boolean> {
    return this.withPermit(async () => {
      try {
        return await verify(passwordHash, password);
      } catch {
        return false;
      }
    });
  }

  /**
   * Performs a verification against a fixed dummy hash so that unknown Online IDs take about as
   * long to reject as wrong passwords, reducing account enumeration through timing.
   */
  async verifyDummy(password: string): Promise<void> {
    this.dummyHash ??= this.hash('akeno-network-dummy-password-for-timing');
    await this.verify(await this.dummyHash, password);
  }

  needsRehash(passwordHash: string): boolean {
    try {
      const parsed = parseOptions(passwordHash);
      return (
        parsed.algorithm !== ARGON2ID ||
        parsed.memoryCost !== this.options.memoryCost ||
        parsed.timeCost !== this.options.timeCost ||
        parsed.parallelism !== this.options.parallelism
      );
    } catch {
      return true;
    }
  }

  private async withPermit<T>(task: () => Promise<T>): Promise<T> {
    if (this.active >= this.options.maxConcurrency) {
      await new Promise<void>((resolve) => this.waiters.push(resolve));
    }
    this.active += 1;
    try {
      return await task();
    } finally {
      this.active -= 1;
      this.waiters.shift()?.();
    }
  }
}
