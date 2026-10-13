import { pino, type Logger } from 'pino';
import type { AppConfig } from '../config/env.js';

/**
 * Structured JSON logging. Cookies, CSRF tokens, passwords and invitation codes are redacted so
 * they never reach log storage. Message bodies are never logged by application code.
 */
export function createLogger(config: Pick<AppConfig, 'logLevel' | 'logPretty'>): Logger {
  return pino({
    level: config.logLevel,
    redact: {
      paths: [
        'req.headers.cookie',
        'req.headers.authorization',
        'req.headers["x-csrf-token"]',
        'res.headers["set-cookie"]',
        '*.password',
        '*.currentPassword',
        '*.newPassword',
        '*.invitationCode',
        '*.rawToken',
        '*.token',
      ],
      censor: '[redacted]',
    },
    ...(config.logPretty
      ? { transport: { target: 'pino-pretty', options: { translateTime: 'SYS:HH:MM:ss', ignore: 'pid,hostname' } } }
      : {}),
  });
}
