import type { FastifyPluginAsync } from 'fastify';
import { z } from 'zod';
import {
  adminUpdateUserSchema,
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

/** Every administration route requires an authenticated administrator session plus CSRF token. */
export function administrationRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAdmin(ctx));

    app.get('/admin/invitations', async () => ({ invitations: await ctx.admin.listInvitations() }));

    // Curated game names; they override names reported by devices for everyone.
    const titleParams = z.object({ titleId: gameTitleIdSchema });
    app.get('/admin/game-titles', async () => ({ titles: await ctx.activity.listTitles() }));
    app.put('/admin/game-titles/:titleId', async (request) => {
      const { titleId } = parseInput(titleParams, request.params);
      const { name } = parseInput(gameTitleNameSchema, request.body);
      return ctx.activity.setTitle(authOf(request).userId, titleId, name);
    });
    app.delete('/admin/game-titles/:titleId', async (request, reply) => {
      const { titleId } = parseInput(titleParams, request.params);
      await ctx.activity.removeTitle(titleId);
      return reply.status(204).send();
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
