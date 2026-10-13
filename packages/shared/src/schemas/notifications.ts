import { z } from 'zod';

export const markNotificationsReadSchema = z.union([
  z.object({ all: z.literal(true) }),
  z.object({ ids: z.array(z.coerce.number().int().min(1)).min(1).max(100) }),
]);
export type MarkNotificationsReadInput = z.infer<typeof markNotificationsReadSchema>;
