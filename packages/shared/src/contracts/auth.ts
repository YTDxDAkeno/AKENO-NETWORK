import type { CurrentUser } from './users.js';

export type RegistrationMode = 'invite' | 'open';

export interface AuthConfigResponse {
  registrationMode: RegistrationMode;
  passwordMinLength: number;
}

export interface AuthSessionResponse {
  user: CurrentUser;
  /** Must be echoed in the X-CSRF-Token header on state-changing requests. Keep in memory only. */
  csrfToken: string;
  session: {
    id: string;
    expiresAt: string;
  };
}

export interface AuthSessionDto {
  id: string;
  createdAt: string;
  lastUsedAt: string;
  expiresAt: string;
  userAgent: string | null;
  ipAddress: string | null;
  current: boolean;
}
