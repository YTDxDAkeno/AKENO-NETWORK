import type { z } from 'zod';
import { Errors } from './errors.js';

/**
 * Validates untrusted input with a Zod schema. On failure, throws a VALIDATION_FAILED error
 * whose message is the first issue and whose `fields` map contains one message per field.
 */
export function parseInput<S extends z.ZodType>(schema: S, input: unknown): z.output<S> {
  const result = schema.safeParse(input ?? {});
  if (result.success) return result.data;
  const fields: Record<string, string> = {};
  for (const issue of result.error.issues) {
    const key = issue.path.length > 0 ? issue.path.join('.') : '_';
    if (!(key in fields)) fields[key] = issue.message;
  }
  const first = result.error.issues[0];
  const message = first ? first.message : 'The request is invalid.';
  throw Errors.validation(message, fields);
}
