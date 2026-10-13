import { z } from 'zod';
import { REALTIME_PROTOCOL_VERSION } from '../constants.js';
import { uuidSchema } from './common.js';

/** Messages a client may send over the WebSocket. Everything else is rejected. */
export const clientRealtimeMessageSchema = z.discriminatedUnion('type', [
  z.object({ v: z.literal(REALTIME_PROTOCOL_VERSION), type: z.literal('ping') }),
  z.object({
    v: z.literal(REALTIME_PROTOCOL_VERSION),
    type: z.literal('presence.idle'),
    data: z.object({ idle: z.boolean() }),
  }),
  z.object({
    v: z.literal(REALTIME_PROTOCOL_VERSION),
    type: z.literal('typing'),
    data: z.object({ conversationId: uuidSchema }),
  }),
]);
export type ClientRealtimeMessage = z.infer<typeof clientRealtimeMessageSchema>;
