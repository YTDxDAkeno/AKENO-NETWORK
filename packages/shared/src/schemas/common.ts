import { z } from 'zod';
import { LIMITS } from '../constants.js';

/** Strips ASCII control characters (except tab/newline handled by callers) from user text. */
// eslint-disable-next-line no-control-regex
const CONTROL_CHARACTERS = /[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g;

export function stripControlCharacters(value: string): string {
  return value.replace(CONTROL_CHARACTERS, '');
}

/** Single-line text: control characters and line breaks removed, whitespace collapsed and trimmed. */
export const singleLineText = (max: number, min = 0) =>
  z
    .string()
    .transform((value) => stripControlCharacters(value).replace(/[\r\n\t]+/g, ' ').replace(/\s{2,}/g, ' ').trim())
    .pipe(
      z
        .string()
        .min(min, min === 1 ? 'This field is required.' : `Must be at least ${min} characters.`)
        .max(max, `Must be at most ${max} characters.`),
    );

/** Multi-line text: control characters removed (newlines kept), trimmed. */
export const multiLineText = (max: number, min = 0) =>
  z
    .string()
    .transform((value) => stripControlCharacters(value).replace(/\r\n/g, '\n').trim())
    .pipe(
      z
        .string()
        .min(min, min === 1 ? 'This field is required.' : `Must be at least ${min} characters.`)
        .max(max, `Must be at most ${max} characters.`),
    );

/** UUIDs are normalized to lowercase so ordered-pair comparisons match PostgreSQL's ordering. */
export const uuidSchema = z.uuid('Must be a valid identifier.').transform((value) => value.toLowerCase());

export const idParamsSchema = z.object({ id: uuidSchema });
export const userIdParamsSchema = z.object({ userId: uuidSchema });
export const idAndUserParamsSchema = z.object({ id: uuidSchema, userId: uuidSchema });

export const limitSchema = z.coerce.number().int().min(1).max(LIMITS.pageSizeMax);

export const userTargetSchema = z.object({ userId: uuidSchema });
export type UserTargetInput = z.infer<typeof userTargetSchema>;
