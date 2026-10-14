import type { FastifyPluginAsync } from 'fastify';
import {
  approveDevicePairingSchema,
  devicePairingCodeSchema,
  devicePairingTokenSchema,
  idParamsSchema,
  startDevicePairingSchema,
  updateDeviceScopesSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requestMeta, requireDevice, requireSession } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

/**
 * Device pairing (versioned extension of API v1).
 *
 * Unauthenticated device endpoints:  POST /devices/pairings, POST /devices/pairings/token
 * Signed-in website endpoints:       POST /devices/pairings/lookup | approve | deny,
 *                                    GET /devices, PATCH /devices/:id (permissions), DELETE /devices/:id
 * Paired-device endpoints:           GET /devices/current, DELETE /devices/current
 */
export function devicesRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    const session = requireSession(ctx);
    const device = requireDevice();

    app.post(
      '/devices/pairings',
      { config: { rateLimit: { max: 10, timeWindow: '15 minutes' } } },
      async (request, reply) => {
        const input = parseInput(startDevicePairingSchema, request.body);
        return reply.status(201).send(await ctx.devices.startPairing(input, requestMeta(request)));
      },
    );

    app.post(
      '/devices/pairings/token',
      { config: { rateLimit: { max: 30, timeWindow: '1 minute' } } },
      async (request) => {
        const { deviceCode } = parseInput(devicePairingTokenSchema, request.body);
        return ctx.devices.redeemPairing(deviceCode, requestMeta(request));
      },
    );

    // Codes are sent in request bodies rather than URLs so they never appear in access logs.
    app.post(
      '/devices/pairings/lookup',
      { preHandler: session, config: { rateLimit: { max: 10, timeWindow: '1 minute' } } },
      async (request) => {
        const { userCode } = parseInput(devicePairingCodeSchema, request.body);
        return { pairing: await ctx.devices.previewPairing(userCode) };
      },
    );

    app.post(
      '/devices/pairings/approve',
      { preHandler: session, config: { rateLimit: { max: 10, timeWindow: '1 minute' } } },
      async (request, reply) => {
        const input = parseInput(approveDevicePairingSchema, request.body);
        await ctx.devices.approvePairing(authOf(request), input, requestMeta(request));
        return reply.status(204).send();
      },
    );

    app.post(
      '/devices/pairings/deny',
      { preHandler: session, config: { rateLimit: { max: 10, timeWindow: '1 minute' } } },
      async (request, reply) => {
        const { userCode } = parseInput(devicePairingCodeSchema, request.body);
        await ctx.devices.denyPairing(authOf(request), userCode, requestMeta(request));
        return reply.status(204).send();
      },
    );

    app.get('/devices', { preHandler: session }, async (request) => ({
      devices: await ctx.devices.list(authOf(request).userId, null),
    }));

    // Reauthorization: the owner changes what an already paired device may do (website only).
    app.patch('/devices/:id', { preHandler: session, config: { rateLimit: { max: 20, timeWindow: '1 minute' } } }, async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { scopes } = parseInput(updateDeviceScopesSchema, request.body);
      return { device: await ctx.devices.updateScopes(authOf(request).userId, id, scopes, requestMeta(request)) };
    });

    app.delete('/devices/:id', { preHandler: session }, async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.devices.revoke(authOf(request).userId, id, requestMeta(request));
      return reply.status(204).send();
    });

    app.get('/devices/current', { preHandler: device }, async (request) => ctx.devices.describeSelf(authOf(request)));

    app.delete('/devices/current', { preHandler: device }, async (request, reply) => {
      const identity = authOf(request);
      await ctx.devices.revoke(identity.userId, identity.sessionId, requestMeta(request), 'This device signed out.');
      return reply.status(204).send();
    });
  };
}
