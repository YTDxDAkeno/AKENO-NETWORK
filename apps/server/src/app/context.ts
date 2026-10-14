import type { FastifyBaseLogger } from 'fastify';
import type { AppConfig } from '../config/env.js';
import type { Database } from '../database/client.js';
import { AccountService } from '../modules/accounts/service.js';
import { ActivityService } from '../modules/activity/service.js';
import { AdminService } from '../modules/administration/service.js';
import { AuthService } from '../modules/auth/service.js';
import { BadgesService } from '../modules/badges/service.js';
import { DevicesService } from '../modules/devices/service.js';
import { FriendsService } from '../modules/friends/service.js';
import { GameCatalogService } from '../modules/games/service.js';
import { GameSessionsService } from '../modules/game-sessions/service.js';
import { GroupsService } from '../modules/groups/service.js';
import { InvitationsService } from '../modules/invitations/service.js';
import { MediaService } from '../modules/media/service.js';
import { MessagesService } from '../modules/messages/service.js';
import { NotificationsService } from '../modules/notifications/service.js';
import { PartiesService } from '../modules/parties/service.js';
import { RelationsService } from '../modules/users/relations.js';
import { UsersService } from '../modules/users/service.js';
import { RealtimeHub } from '../realtime/hub.js';
import { PresenceService } from '../realtime/presence.js';
import { AuditService } from '../services/audit.js';
import { PasswordService } from '../services/password.js';

/**
 * Application services wired together for one backend instance. Services receive the context
 * and resolve their collaborators lazily, which keeps construction order simple.
 */
export interface AppContext {
  config: AppConfig;
  db: Database;
  log: FastifyBaseLogger;
  passwords: PasswordService;
  audit: AuditService;
  hub: RealtimeHub;
  presence: PresenceService;
  relations: RelationsService;
  users: UsersService;
  notifications: NotificationsService;
  auth: AuthService;
  devices: DevicesService;
  friends: FriendsService;
  messages: MessagesService;
  groups: GroupsService;
  parties: PartiesService;
  gameSessions: GameSessionsService;
  invitations: InvitationsService;
  accounts: AccountService;
  admin: AdminService;
  activity: ActivityService;
  media: MediaService;
  games: GameCatalogService;
  badges: BadgesService;
}

export function createContext(config: AppConfig, db: Database, log: FastifyBaseLogger): AppContext {
  const ctx = { config, db, log } as AppContext;
  ctx.passwords = new PasswordService(config.argon2);
  ctx.audit = new AuditService(db, log);
  ctx.hub = new RealtimeHub();
  ctx.presence = new PresenceService(ctx);
  ctx.relations = new RelationsService(ctx);
  ctx.users = new UsersService(ctx);
  ctx.notifications = new NotificationsService(ctx);
  ctx.auth = new AuthService(ctx);
  ctx.devices = new DevicesService(ctx);
  ctx.friends = new FriendsService(ctx);
  ctx.messages = new MessagesService(ctx);
  ctx.groups = new GroupsService(ctx);
  ctx.parties = new PartiesService(ctx);
  ctx.gameSessions = new GameSessionsService(ctx);
  ctx.invitations = new InvitationsService(ctx);
  ctx.accounts = new AccountService(ctx);
  ctx.admin = new AdminService(ctx);
  ctx.activity = new ActivityService(ctx);
  ctx.media = new MediaService(ctx);
  ctx.games = new GameCatalogService(ctx);
  ctx.badges = new BadgesService(ctx);
  return ctx;
}
