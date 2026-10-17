import { z } from 'zod';
import { SHAREPLAY_QUALITIES, type SharePlayQuality } from '@akeno/shared';

/**
 * Server configuration, read once from environment variables and validated at startup.
 * Invalid or missing settings stop the process with an English error message instead of
 * starting in an insecure or half-configured state.
 */

const booleanString = z
  .enum(['true', 'false', '1', '0', 'yes', 'no'])
  .transform((value) => value === 'true' || value === '1' || value === 'yes');

const envSchema = z
  .object({
    NODE_ENV: z.enum(['development', 'test', 'production']).default('development'),
    HOST: z.string().default('127.0.0.1'),
    PORT: z.coerce.number().int().min(0).max(65535).default(3000),
    LOG_LEVEL: z.enum(['fatal', 'error', 'warn', 'info', 'debug', 'trace', 'silent']).default('info'),
    LOG_PRETTY: booleanString.optional(),

    DATABASE_URL: z.string().min(1, 'DATABASE_URL is required (for example postgres://user:password@host:5432/db).'),
    DB_POOL_MAX: z.coerce.number().int().min(1).max(100).default(10),

    /** Canonical public origin of the web client, e.g. https://akeno.example.com */
    PUBLIC_ORIGIN: z.url('PUBLIC_ORIGIN must be a full URL such as https://akeno.example.com'),
    /** Optional comma-separated list of additional allowed origins (e.g. LAN addresses). */
    ADDITIONAL_ORIGINS: z.string().default(''),
    /**
     * Which reverse proxies may set X-Forwarded-For: "false", or a comma-separated list of proxy
     * IPs/CIDRs or proxy-addr names such as "loopback,uniquelocal". Hop counts are not supported
     * because Fastify treats them as untrusted.
     */
    TRUST_PROXY: z
      .string()
      .default('false')
      .refine((value) => !/^\s*\d+\s*$/.test(value) || value.trim() === '0', {
        message: 'TRUST_PROXY hop counts are not supported. Use proxy IP ranges such as "loopback,uniquelocal".',
      }),

    REGISTRATION_MODE: z.enum(['invite', 'open']).default('invite'),
    ALLOW_OPEN_REGISTRATION_IN_PRODUCTION: booleanString.default(false),

    SESSION_TTL_DAYS: z.coerce.number().int().min(1).max(365).default(30),
    SESSION_IDLE_TIMEOUT_DAYS: z.coerce.number().int().min(1).max(365).default(7),
    /** "auto" derives the Secure flag from PUBLIC_ORIGIN (https => secure). */
    COOKIE_SECURE: z.enum(['auto', 'true', 'false']).default('auto'),

    /** Paired-device tokens: absolute lifetime and idle timeout. */
    DEVICE_TOKEN_TTL_DAYS: z.coerce.number().int().min(1).max(365).default(180),
    DEVICE_TOKEN_IDLE_DAYS: z.coerce.number().int().min(1).max(365).default(30),

    ARGON2_MEMORY_KIB: z.coerce.number().int().min(1024).max(1_048_576).default(19_456),
    ARGON2_TIME_COST: z.coerce.number().int().min(1).max(10).default(2),
    ARGON2_PARALLELISM: z.coerce.number().int().min(1).max(8).default(1),
    ARGON2_MAX_CONCURRENCY: z.coerce.number().int().min(1).max(32).default(4),

    RATE_LIMIT_ENABLED: booleanString.default(true),
    RATE_LIMIT_MAX_PER_MINUTE: z.coerce.number().int().min(10).max(100_000).default(300),

    /** Persistent directory for uploaded images (profile pictures, banners, game artwork). */
    MEDIA_DIR: z.string().default('./data/media'),
    /** Upper bound for all stored media together; uploads are refused beyond it. */
    MEDIA_MAX_TOTAL_MB: z.coerce.number().int().min(16).max(1_000_000).default(2048),
    /** Uploads are refused when the media volume has less free space than this. */
    MEDIA_MIN_FREE_MB: z.coerce.number().int().min(0).max(1_000_000).default(512),
    /** Concurrent image decodes (each may use up to ~150 MB of memory for the largest inputs). */
    MEDIA_PROCESSING_CONCURRENCY: z.coerce.number().int().min(1).max(8).default(1),

    /**
     * Voice parties (LiveKit SFU). All three of URL, key and secret enable voice; leave them empty
     * to run without voice. LIVEKIT_URL is what browsers use (wss://<domain>/livekit behind Caddy);
     * LIVEKIT_API_URL is how this server reaches LiveKit's API (default: LIVEKIT_URL over http).
     */
    LIVEKIT_URL: z.string().default(''),
    LIVEKIT_API_URL: z.string().default(''),
    LIVEKIT_API_KEY: z.string().default(''),
    LIVEKIT_API_SECRET: z.string().default(''),
    /** Lifetime of a voice join token. Connected participants are kept connected by LiveKit. */
    VOICE_TOKEN_TTL_SECONDS: z.coerce.number().int().min(30).max(600).default(120),

    /**
     * SharePlay (Alpha 0.5, experimental): party gameplay streaming over the same LiveKit server
     * (needs the LIVEKIT_* settings). Off unless explicitly enabled. Remote controller input is a
     * second, separate switch. See docs/SHAREPLAY_ARCHITECTURE.md.
     */
    SHAREPLAY_ENABLED: booleanString.default(false),
    SHAREPLAY_MAX_VIEWERS: z.coerce.number().int().min(1).max(15).default(4),
    SHAREPLAY_MAX_QUALITY: z.enum(SHAREPLAY_QUALITIES).default('720p60'),
    SHAREPLAY_REMOTE_INPUT_ENABLED: booleanString.default(false),
    /** Longest a Player 2 grant lasts before the host has to approve again. */
    SHAREPLAY_GRANT_MAX_MINUTES: z.coerce.number().int().min(5).max(720).default(120),

    /**
     * LAN Play (experimental) control plane. Off unless explicitly enabled. Even when enabled, no
     * network transport is wired, so it reports "Transport not available". See
     * docs/LAN_PLAY_ARCHITECTURE.md.
     */
    LAN_PLAY_ENABLED: booleanString.default(false),
    LAN_PLAY_MAX_PARTICIPANTS: z.coerce.number().int().min(2).max(16).default(8),
    LAN_PLAY_SESSION_TTL_MINUTES: z.coerce.number().int().min(5).max(1440).default(240),
    LAN_PLAY_CREDENTIAL_TTL_SECONDS: z.coerce.number().int().min(30).max(3600).default(120),

    /** Directory containing the built web client (apps/web/dist). Empty disables static hosting. */
    WEB_DIST_DIR: z.string().default(''),
    MIGRATE_ON_START: booleanString.default(false),
    MIGRATIONS_DIR: z.string().default(''),
  })
  .superRefine((env, ctx) => {
    if (env.NODE_ENV === 'production' && env.REGISTRATION_MODE === 'open' && !env.ALLOW_OPEN_REGISTRATION_IN_PRODUCTION) {
      ctx.addIssue({
        code: 'custom',
        path: ['REGISTRATION_MODE'],
        message:
          'Open registration is disabled in production. Use REGISTRATION_MODE=invite or explicitly set ALLOW_OPEN_REGISTRATION_IN_PRODUCTION=true.',
      });
    }
    const voice = [env.LIVEKIT_URL, env.LIVEKIT_API_KEY, env.LIVEKIT_API_SECRET].filter((value) => value.trim() !== '');
    if (voice.length > 0 && voice.length < 3) {
      ctx.addIssue({
        code: 'custom',
        path: ['LIVEKIT_URL'],
        message: 'Voice needs LIVEKIT_URL, LIVEKIT_API_KEY and LIVEKIT_API_SECRET together (or none of them to disable voice).',
      });
    }
    if (env.SHAREPLAY_ENABLED && voice.length < 3) {
      ctx.addIssue({ code: 'custom', path: ['SHAREPLAY_ENABLED'], message: 'SharePlay needs the voice server: set LIVEKIT_URL, LIVEKIT_API_KEY and LIVEKIT_API_SECRET.' });
    }
    if (env.SHAREPLAY_REMOTE_INPUT_ENABLED && !env.SHAREPLAY_ENABLED) {
      ctx.addIssue({ code: 'custom', path: ['SHAREPLAY_REMOTE_INPUT_ENABLED'], message: 'SHAREPLAY_REMOTE_INPUT_ENABLED needs SHAREPLAY_ENABLED=true.' });
    }
    // Both end up in LiveKit's YAML configuration (compose voice profile): keep them plain.
    for (const key of ['LIVEKIT_API_KEY', 'LIVEKIT_API_SECRET'] as const) {
      if (env[key] && !/^[A-Za-z0-9_-]+$/.test(env[key])) {
        ctx.addIssue({ code: 'custom', path: [key], message: `${key} may only contain letters, digits, "-" and "_".` });
      }
    }
    if (env.LIVEKIT_API_SECRET && env.LIVEKIT_API_SECRET.length < 32) {
      ctx.addIssue({ code: 'custom', path: ['LIVEKIT_API_SECRET'], message: 'LIVEKIT_API_SECRET must be at least 32 characters.' });
    }
    if (env.LIVEKIT_URL && (!/^wss?:\/\//.test(env.LIVEKIT_URL) || !URL.canParse(env.LIVEKIT_URL))) {
      ctx.addIssue({ code: 'custom', path: ['LIVEKIT_URL'], message: 'LIVEKIT_URL must start with wss:// (or ws:// for local development).' });
    }
    if (env.NODE_ENV === 'production' && env.LIVEKIT_URL.startsWith('ws://') && !/^ws:\/\/(localhost|127\.0\.0\.1)/.test(env.LIVEKIT_URL)) {
      ctx.addIssue({ code: 'custom', path: ['LIVEKIT_URL'], message: 'Use wss:// for LIVEKIT_URL in production.' });
    }
    if (env.NODE_ENV === 'production' && env.COOKIE_SECURE === 'false') {
      ctx.addIssue({
        code: 'custom',
        path: ['COOKIE_SECURE'],
        message: 'COOKIE_SECURE=false is not allowed in production. Serve AKENO NETWORK over HTTPS.',
      });
    }
  });

export interface AppConfig {
  env: 'development' | 'test' | 'production';
  isProduction: boolean;
  host: string;
  port: number;
  logLevel: string;
  logPretty: boolean;
  databaseUrl: string;
  dbPoolMax: number;
  publicOrigin: string;
  allowedOrigins: string[];
  trustProxy: boolean | string;
  registrationMode: 'invite' | 'open';
  session: {
    ttlMs: number;
    idleTimeoutMs: number;
    cookieName: string;
    cookieSecure: boolean;
    /** Minimum interval between last_used_at updates for an active session. */
    touchIntervalMs: number;
    maxPerUser: number;
  };
  devices: {
    ttlMs: number;
    idleTimeoutMs: number;
  };
  argon2: {
    memoryCost: number;
    timeCost: number;
    parallelism: number;
    maxConcurrency: number;
  };
  rateLimit: {
    enabled: boolean;
    maxPerMinute: number;
  };
  voice: {
    enabled: boolean;
    publicUrl: string;
    apiUrl: string;
    apiKey: string;
    apiSecret: string;
    tokenTtlSeconds: number;
  };
  shareplay: {
    enabled: boolean;
    maxViewers: number;
    maxQuality: SharePlayQuality;
    remoteInputEnabled: boolean;
    grantMaxMs: number;
  };
  lanplay: {
    enabled: boolean;
    maxParticipants: number;
    sessionTtlMs: number;
    credentialTtlMs: number;
  };
  media: {
    dir: string;
    maxTotalBytes: number;
    minFreeBytes: number;
    concurrency: number;
  };
  webDistDir: string | null;
  migrateOnStart: boolean;
  migrationsDir: string | null;
}

function normalizeOrigin(value: string): string {
  const url = new URL(value.trim());
  return url.origin;
}

function parseTrustProxy(value: string): boolean | string {
  const trimmed = value.trim();
  if (trimmed === '' || trimmed === 'false' || trimmed === '0') return false;
  if (trimmed === 'true') return true;
  return trimmed;
}

export function loadConfig(source: NodeJS.ProcessEnv = process.env): AppConfig {
  const parsed = envSchema.safeParse(source);
  if (!parsed.success) {
    const details = parsed.error.issues.map((issue) => `  - ${issue.path.join('.') || 'config'}: ${issue.message}`).join('\n');
    throw new Error(`Invalid AKENO NETWORK configuration:\n${details}`);
  }
  const env = parsed.data;
  const publicOrigin = normalizeOrigin(env.PUBLIC_ORIGIN);
  const additional = env.ADDITIONAL_ORIGINS.split(',')
    .map((entry) => entry.trim())
    .filter(Boolean)
    .map(normalizeOrigin);
  const cookieSecure = env.COOKIE_SECURE === 'auto' ? publicOrigin.startsWith('https://') : env.COOKIE_SECURE === 'true';

  return {
    env: env.NODE_ENV,
    isProduction: env.NODE_ENV === 'production',
    host: env.HOST,
    port: env.PORT,
    logLevel: env.LOG_LEVEL,
    logPretty: env.LOG_PRETTY ?? env.NODE_ENV === 'development',
    databaseUrl: env.DATABASE_URL,
    dbPoolMax: env.DB_POOL_MAX,
    publicOrigin,
    allowedOrigins: Array.from(new Set([publicOrigin, ...additional])),
    trustProxy: parseTrustProxy(env.TRUST_PROXY),
    registrationMode: env.REGISTRATION_MODE,
    session: {
      ttlMs: env.SESSION_TTL_DAYS * 24 * 60 * 60 * 1000,
      idleTimeoutMs: env.SESSION_IDLE_TIMEOUT_DAYS * 24 * 60 * 60 * 1000,
      // The __Host- prefix forces Secure, Path=/ and no Domain attribute in browsers.
      cookieName: cookieSecure ? '__Host-akeno_session' : 'akeno_session',
      cookieSecure,
      touchIntervalMs: 5 * 60 * 1000,
      maxPerUser: 20,
    },
    devices: {
      ttlMs: env.DEVICE_TOKEN_TTL_DAYS * 24 * 60 * 60 * 1000,
      idleTimeoutMs: env.DEVICE_TOKEN_IDLE_DAYS * 24 * 60 * 60 * 1000,
    },
    argon2: {
      memoryCost: env.ARGON2_MEMORY_KIB,
      timeCost: env.ARGON2_TIME_COST,
      parallelism: env.ARGON2_PARALLELISM,
      maxConcurrency: env.ARGON2_MAX_CONCURRENCY,
    },
    rateLimit: {
      enabled: env.RATE_LIMIT_ENABLED,
      maxPerMinute: env.RATE_LIMIT_MAX_PER_MINUTE,
    },
    voice: {
      enabled: Boolean(env.LIVEKIT_URL && env.LIVEKIT_API_KEY && env.LIVEKIT_API_SECRET),
      publicUrl: env.LIVEKIT_URL.trim(),
      apiUrl: (env.LIVEKIT_API_URL.trim() || env.LIVEKIT_URL.trim()).replace(/^ws(s?):\/\//, 'http$1://'),
      apiKey: env.LIVEKIT_API_KEY.trim(),
      apiSecret: env.LIVEKIT_API_SECRET.trim(),
      tokenTtlSeconds: env.VOICE_TOKEN_TTL_SECONDS,
    },
    shareplay: {
      enabled: env.SHAREPLAY_ENABLED,
      maxViewers: env.SHAREPLAY_MAX_VIEWERS,
      maxQuality: env.SHAREPLAY_MAX_QUALITY,
      remoteInputEnabled: env.SHAREPLAY_REMOTE_INPUT_ENABLED,
      grantMaxMs: env.SHAREPLAY_GRANT_MAX_MINUTES * 60_000,
    },
    lanplay: {
      enabled: env.LAN_PLAY_ENABLED,
      maxParticipants: env.LAN_PLAY_MAX_PARTICIPANTS,
      sessionTtlMs: env.LAN_PLAY_SESSION_TTL_MINUTES * 60_000,
      credentialTtlMs: env.LAN_PLAY_CREDENTIAL_TTL_SECONDS * 1000,
    },
    media: {
      dir: env.MEDIA_DIR.trim(),
      maxTotalBytes: env.MEDIA_MAX_TOTAL_MB * 1024 * 1024,
      minFreeBytes: env.MEDIA_MIN_FREE_MB * 1024 * 1024,
      concurrency: env.MEDIA_PROCESSING_CONCURRENCY,
    },
    webDistDir: env.WEB_DIST_DIR.trim() || null,
    migrateOnStart: env.MIGRATE_ON_START,
    migrationsDir: env.MIGRATIONS_DIR.trim() || null,
  };
}
