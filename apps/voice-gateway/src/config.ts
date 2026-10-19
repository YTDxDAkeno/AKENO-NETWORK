/** Configuration from the environment (see docs/PS5_VOICE.md#operating-the-gateway). */
export interface GatewayConfig {
  host: string;
  port: number;
  /** URL path consoles connect to (behind Caddy: wss://<domain>/voice-gateway). */
  path: string;
  /** How the gateway reaches LiveKit's signalling (internal, e.g. ws://livekit:7880). */
  livekitUrl: string;
  apiKey: string;
  apiSecret: string;
  maxSessions: number;
  logLevel: string;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): GatewayConfig {
  const required = (key: string): string => {
    const value = (env[key] ?? '').trim();
    if (!value) throw new Error(`${key} is required`);
    return value;
  };
  const port = Number(env.GATEWAY_PORT ?? 7890);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('GATEWAY_PORT must be a port number');
  const maxSessions = Number(env.GATEWAY_MAX_SESSIONS ?? 50);
  if (!Number.isInteger(maxSessions) || maxSessions < 1 || maxSessions > 1000) throw new Error('GATEWAY_MAX_SESSIONS must be 1..1000');
  const path = (env.GATEWAY_PATH ?? '/voice-gateway').trim();
  if (!/^\/[A-Za-z0-9/_-]*$/.test(path)) throw new Error('GATEWAY_PATH must be a plain URL path');
  const livekitUrl = required('LIVEKIT_URL');
  if (!/^wss?:\/\//.test(livekitUrl)) throw new Error('LIVEKIT_URL must be a ws:// or wss:// URL');
  const apiSecret = required('LIVEKIT_API_SECRET');
  if (apiSecret.length < 32) throw new Error('LIVEKIT_API_SECRET must be at least 32 characters');
  return {
    host: (env.GATEWAY_HOST ?? '127.0.0.1').trim(),
    port,
    path,
    livekitUrl,
    apiKey: required('LIVEKIT_API_KEY'),
    apiSecret,
    maxSessions,
    logLevel: (env.LOG_LEVEL ?? 'info').trim(),
  };
}
