import type { FastifyPluginAsync } from 'fastify';
import { z } from 'zod';
import {
  BADGE_IDS,
  adminUpdateUserSchema,
  createGameSchema,
  gameArtworkSlotSchema,
  gameListQuerySchema,
  grantBadgeSchema,
  updateGameSchema,
  uuidSchema,
  gameTitleIdSchema,
  gameTitleNameSchema,
  adminUserListQuerySchema,
  createRegistrationInvitationSchema,
  cursorPageQuerySchema,
  idParamsSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAdmin } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';
import { acceptImageUploads, uploadedImage } from '../media/routes.js';

const gameArtworkParams = z.object({ id: uuidSchema, slot: gameArtworkSlotSchema });
const badgeParams = z.object({ id: uuidSchema, badgeId: z.enum(BADGE_IDS) });

/** Every administration route requires an authenticated administrator session plus CSRF token. */
export function administrationRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAdmin(ctx));

    app.get('/admin/invitations', async () => ({ invitations: await ctx.admin.listInvitations() }));

    // Curated game names (Alpha 0.2 API, kept for compatibility); they override names reported
    // by devices for everyone. Each title ID maps to a canonical game in the catalogue.
    const titleParams = z.object({ titleId: gameTitleIdSchema });
    app.get('/admin/game-titles', async () => ({ titles: await ctx.games.listTitleNames() }));
    app.put('/admin/game-titles/:titleId', async (request) => {
      const { titleId } = parseInput(titleParams, request.params);
      const { name } = parseInput(gameTitleNameSchema, request.body);
      return ctx.games.setTitleName(authOf(request).userId, titleId, name, request.ip);
    });
    app.delete('/admin/game-titles/:titleId', async (request, reply) => {
      const { titleId } = parseInput(titleParams, request.params);
      await ctx.games.removeTitleName(authOf(request).userId, titleId, request.ip);
      return reply.status(204).send();
    });

    // Game catalogue and artwork.
    const gameParams = z.object({ id: uuidSchema });
    const gameTitleParams = z.object({ id: uuidSchema, titleId: gameTitleIdSchema });
    app.get('/admin/games', async (request) => {
      const query = parseInput(gameListQuerySchema, request.query);
      return { games: await ctx.games.list(query) };
    });
    app.get('/admin/games/detected-titles', async () => ({ titles: await ctx.games.detectedTitles() }));
    app.post('/admin/games/reload', async (request) => ctx.games.reload(authOf(request).userId, request.ip));
    app.post('/admin/games', async (request, reply) => {
      const input = parseInput(createGameSchema, request.body);
      return reply.status(201).send(await ctx.games.create(authOf(request).userId, input, request.ip));
    });
    app.get('/admin/games/:id', async (request) => {
      const { id } = parseInput(gameParams, request.params);
      return ctx.games.get(id);
    });
    app.patch('/admin/games/:id', async (request) => {
      const { id } = parseInput(gameParams, request.params);
      const input = parseInput(updateGameSchema, request.body);
      return ctx.games.update(authOf(request).userId, id, input, request.ip);
    });
    app.delete('/admin/games/:id', async (request, reply) => {
      const { id } = parseInput(gameParams, request.params);
      await ctx.games.remove(authOf(request).userId, id, request.ip);
      return reply.status(204).send();
    });
    app.put('/admin/games/:id/titles/:titleId', async (request) => {
      const { id, titleId } = parseInput(gameTitleParams, request.params);
      return ctx.games.mapTitle(authOf(request).userId, id, titleId, request.ip);
    });
    app.delete('/admin/games/:id/titles/:titleId', async (request, reply) => {
      const { titleId } = parseInput(gameTitleParams, request.params);
      await ctx.games.unmapTitle(authOf(request).userId, titleId, request.ip);
      return reply.status(204).send();
    });
    app.delete('/admin/games/:id/artwork/:slot', async (request) => {
      const { id, slot } = parseInput(gameArtworkParams, request.params);
      return ctx.games.removeArtwork(authOf(request).userId, id, slot, request.ip);
    });

    // Badges (audited; administrators cannot give themselves authority badges).
    app.get('/admin/users/:id/badges', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return { badges: await ctx.badges.listForUser(id) };
    });
    app.post('/admin/users/:id/badges', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { badgeId, note } = parseInput(grantBadgeSchema, request.body);
      await ctx.badges.grant(authOf(request).userId, id, badgeId, note, request.ip);
      return reply.status(201).send({ badges: await ctx.badges.listForUser(id) });
    });
    app.delete('/admin/users/:id/badges/:badgeId', async (request, reply) => {
      const { id, badgeId } = parseInput(badgeParams, request.params);
      await ctx.badges.revoke(authOf(request).userId, id, badgeId, request.ip);
      return reply.status(204).send();
    });

    // Artwork uploads take a raw image body; the parser exists only in this nested scope.
    await app.register(async (uploads) => {
      acceptImageUploads(uploads, { adminOnly: true });
      uploads.put('/admin/games/:id/artwork/:slot', { config: { rateLimit: { max: 60, timeWindow: '10 minutes' } } }, async (request) => {
        const { id, slot } = parseInput(gameArtworkParams, request.params);
        return ctx.games.setArtwork(authOf(request).userId, id, slot, uploadedImage(request), request.ip);
      });
    });

    app.post('/admin/invitations', async (request, reply) => {
      const input = parseInput(createRegistrationInvitationSchema, request.body);
      return reply.status(201).send(await ctx.admin.createInvitation(authOf(request).userId, input, request.ip));
    });

    app.delete('/admin/invitations/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.admin.revokeInvitation(authOf(request).userId, id, request.ip);
    });

    app.get('/admin/users', async (request) => {
      const { q, limit } = parseInput(adminUserListQuerySchema, request.query);
      return { users: await ctx.admin.listUsers(q, limit) };
    });

    app.patch('/admin/users/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const input = parseInput(adminUpdateUserSchema, request.body);
      return ctx.admin.updateUser(authOf(request).userId, id, input, request.ip);
    });

    app.get('/admin/audit-events', async (request) => {
      const { before, limit } = parseInput(cursorPageQuerySchema, request.query);
      return ctx.admin.listAuditEvents(before, limit);
    });

    app.get('/admin/stats', async () => ctx.admin.stats());
  };
}
