/**
 * Test database configuration. Tests need a PostgreSQL role that may CREATE DATABASE.
 * Override with TEST_DATABASE_ADMIN_URL, e.g. postgres://akeno:password@127.0.0.1:5432/postgres
 */
export const TEST_ADMIN_URL =
  process.env.TEST_DATABASE_ADMIN_URL ?? 'postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres';

export const TEMPLATE_DATABASE = 'akeno_test_template';

export const TEST_ORIGIN = 'http://localhost:5173';

export function databaseUrl(name: string): string {
  const url = new URL(TEST_ADMIN_URL);
  url.pathname = `/${name}`;
  return url.toString();
}
