/** Extracts the underlying PostgreSQL error from driver or ORM wrappers. */
export function getPgError(error: unknown): { code?: string; constraint?: string } | null {
  let current: unknown = error;
  for (let depth = 0; depth < 4 && current; depth += 1) {
    if (typeof current === 'object' && current !== null && 'code' in current && typeof current.code === 'string') {
      const candidate = current as { code: string; constraint?: string };
      if (/^[0-9A-Z]{5}$/.test(candidate.code)) return candidate;
    }
    current = typeof current === 'object' && current !== null && 'cause' in current ? current.cause : null;
  }
  return null;
}

export function isUniqueViolation(error: unknown, constraint?: string): boolean {
  const pg = getPgError(error);
  if (!pg || pg.code !== '23505') return false;
  return constraint === undefined || pg.constraint === constraint;
}

export function isCheckViolation(error: unknown, constraint?: string): boolean {
  const pg = getPgError(error);
  if (!pg || pg.code !== '23514') return false;
  return constraint === undefined || pg.constraint === constraint;
}

export function isForeignKeyViolation(error: unknown): boolean {
  return getPgError(error)?.code === '23503';
}
