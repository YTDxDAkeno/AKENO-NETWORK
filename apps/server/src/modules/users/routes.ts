import type { FastifyPluginAsync } from 'fastify';
import { z } from 'zod';
import {
  deleteAccountSchema,
  updatePresenceSchema,
  updatePrivacySchema,
  updateProfileSchema,
  userSearchSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, clearSessionCookie, requestMeta, requireAuth } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

const handleParams = z.object({ handle: z.string().trim().min(1).max(64) });

export function usersRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    app.get('/users/me', async (request) => ctx.users.getCurrentUser(authOf(request).userId));

    app.delete(
      '/users/me',
      { config: { rateLimit: { max: 5, timeWindow: '15 minutes' } } },
      async (request, reply) => {
        const input = parseInput(deleteAccountSchema, request.body);
        await ctx.accounts.deleteAccount(authOf(request), input, requestMeta(request));
        clearSessionCookie(reply, ctx);
        return reply.status(204).send();
      },
    );

    app.get('/users/search', async (request) => {
      const { q, limit } = parseInput(userSearchSchema, request.query);
      return { users: await ctx.users.search(authOf(request).userId, q, limit) };
    });

    app.get('/users/:handle', async (request) => {
      const { handle } = parseInput(handleParams, request.params);
      return ctx.users.getPublicProfile(authOf(request).userId, handle);
    });

    app.patch('/profile', async (request) => {
      const input = parseInput(updateProfileSchema, request.body);
      return ctx.users.updateProfile(authOf(request).userId, input);
    });

    app.patch('/profile/privacy', async (request) => {
      const input = parseInput(updatePrivacySchema, request.body);
      return ctx.users.updatePrivacy(authOf(request).userId, input);
    });

    app.put('/presence', async (request) => {
      const { status } = parseInput(updatePresenceSchema, request.body);
      const effective = await ctx.presence.setPreference(authOf(request).userId, status);
      return { preference: status, status: effective };
    });
  };
}
