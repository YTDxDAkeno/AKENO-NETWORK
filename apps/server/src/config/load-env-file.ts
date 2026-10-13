import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

/**
 * Development convenience: loads KEY=VALUE pairs from a .env file without overriding variables
 * that are already set. Production deployments pass configuration through the environment.
 */
export function loadEnvFiles(candidates: string[] = ['.env', '../../.env']): string | null {
  for (const candidate of candidates) {
    const path = resolve(process.cwd(), candidate);
    if (!existsSync(path)) continue;
    for (const line of readFileSync(path, 'utf8').split(/\r?\n/)) {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith('#')) continue;
      const index = trimmed.indexOf('=');
      if (index <= 0) continue;
      const key = trimmed.slice(0, index).trim();
      let value = trimmed.slice(index + 1).trim();
      if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) {
        value = value.slice(1, -1);
      }
      if (process.env[key] === undefined) process.env[key] = value;
    }
    return path;
  }
  return null;
}
