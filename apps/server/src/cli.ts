import { createInterface } from 'node:readline';
import { parseArgs } from 'node:util';
import { and, eq } from 'drizzle-orm';
import { ONLINE_ID_PATTERN, checkPasswordQuality, passwordSchema } from '@akeno/shared';
import { createContext } from './app/context.js';
import { createLogger } from './app/logger.js';
import { loadConfig } from './config/env.js';
import { loadEnvFiles } from './config/load-env-file.js';
import { createDatabase } from './database/client.js';
import { resolveMigrationsFolder, runMigrations } from './database/migrate.js';
import { authSessions, friendships, groupMembers, users } from './database/schema/index.js';
import { orderedPair } from './utils/pairs.js';

const HELP = `AKENO NETWORK operator CLI

Usage: akeno-cli <command> [options]

Commands:
  migrate                         Apply pending database migrations.
  create-admin --online-id <id>   Create the first (or another) administrator account.
       [--display-name <name>]    Optional display name.
       [--password-stdin]         Read the password from standard input instead of prompting.
  promote --online-id <id>        Grant administrator rights to an existing account.
  create-invitation               Create a registration invitation code.
       [--max-uses <n>]           How many accounts may use the code (default 1).
       [--expires-in-hours <h>]   Lifetime in hours (default 72, 0 = never expires).
       [--note <text>]            Optional note shown in the admin panel.
  reset-password --online-id <id> Set a new password and sign the account out everywhere.
       [--password-stdin]
  seed                            Create demo data (development only; refused in production).
  help                            Show this help.

Environment: DATABASE_URL is required. A .env file in the current directory or the
repository root is loaded automatically for local development.`;

function fail(message: string): never {
  console.error(`Error: ${message}`);
  process.exit(1);
}

async function readStdin(): Promise<string> {
  const chunks: Buffer[] = [];
  for await (const chunk of process.stdin) chunks.push(chunk as Buffer);
  return Buffer.concat(chunks).toString('utf8').replace(/\r?\n$/, '');
}

async function promptHidden(question: string): Promise<string> {
  const rl = createInterface({ input: process.stdin, output: process.stdout, terminal: true });
  const writable = rl as unknown as { _writeToOutput: (text: string) => void };
  let muted = false;
  writable._writeToOutput = (text: string) => {
    if (!muted) process.stdout.write(text);
  };
  process.stdout.write(question);
  muted = true;
  const answer = await new Promise<string>((resolve) => rl.question('', resolve));
  muted = false;
  process.stdout.write('\n');
  rl.close();
  return answer;
}

async function obtainPassword(onlineId: string, fromStdin: boolean): Promise<string> {
  let password: string;
  if (fromStdin || !process.stdin.isTTY) {
    password = await readStdin();
  } else {
    password = await promptHidden('New password: ');
    const confirmation = await promptHidden('Repeat password: ');
    if (password !== confirmation) fail('The passwords do not match.');
  }
  const parsed = passwordSchema.safeParse(password);
  if (!parsed.success) fail(parsed.error.issues[0]?.message ?? 'Invalid password.');
  const quality = checkPasswordQuality(password, onlineId);
  if (quality) fail(quality);
  return password;
}

async function main(): Promise<void> {
  loadEnvFiles();
  // The CLI never serves HTTP, so a placeholder origin is sufficient when none is configured.
  process.env.PUBLIC_ORIGIN ??= 'http://localhost';
  const [command = 'help', ...rest] = process.argv.slice(2);
  const { values } = parseArgs({
    args: rest,
    options: {
      'online-id': { type: 'string' },
      'display-name': { type: 'string' },
      'password-stdin': { type: 'boolean', default: false },
      'max-uses': { type: 'string' },
      'expires-in-hours': { type: 'string' },
      note: { type: 'string' },
    },
    allowPositionals: false,
    strict: true,
  });

  if (command === 'help' || command === '--help') {
    console.log(HELP);
    return;
  }

  const config = loadConfig();
  const logger = createLogger({ logLevel: 'warn', logPretty: false });
  const database = createDatabase({ url: config.databaseUrl, max: 2, applicationName: 'akeno-cli' });
  const ctx = createContext(config, database.db, logger);

  try {
    switch (command) {
      case 'migrate': {
        await runMigrations(database.db, resolveMigrationsFolder(config.migrationsDir));
        console.log('Database migrations are up to date.');
        break;
      }

      case 'create-admin': {
        const onlineId = values['online-id']?.trim();
        if (!onlineId || !ONLINE_ID_PATTERN.test(onlineId)) {
          fail('Provide a valid --online-id (3-16 characters, starting with a letter).');
        }
        const password = await obtainPassword(onlineId, values['password-stdin'] === true);
        const user = await ctx.accounts.createAccount({
          onlineId,
          password,
          role: 'admin',
          ...(values['display-name'] ? { displayName: values['display-name'].slice(0, 32) } : {}),
        });
        console.log(`Administrator "${onlineId}" created (id ${user.id}). Sign in through the web app.`);
        break;
      }

      case 'promote': {
        const onlineId = values['online-id']?.trim().toLowerCase();
        if (!onlineId) fail('Provide --online-id.');
        const updated = await database.db
          .update(users)
          .set({ role: 'admin', updatedAt: new Date() })
          .where(and(eq(users.onlineIdNormalized, onlineId), eq(users.status, 'active')))
          .returning({ id: users.id });
        if (updated.length === 0) fail(`No active account with Online ID "${onlineId}".`);
        await ctx.audit.record({ actorId: null, action: 'cli.user_promoted', targetType: 'user', targetId: updated[0]!.id });
        console.log(`"${onlineId}" is now an administrator.`);
        break;
      }

      case 'create-invitation': {
        const maxUses = values['max-uses'] ? Number(values['max-uses']) : 1;
        const hours = values['expires-in-hours'] !== undefined ? Number(values['expires-in-hours']) : 72;
        if (!Number.isInteger(maxUses) || maxUses < 1 || maxUses > 100) fail('--max-uses must be between 1 and 100.');
        if (!Number.isInteger(hours) || hours < 0 || hours > 2160) fail('--expires-in-hours must be between 0 and 2160.');
        const created = await ctx.admin.createInvitation(
          null,
          { maxUses, expiresInHours: hours === 0 ? null : hours, note: (values.note ?? 'Created with the CLI').slice(0, 100) },
          null,
        );
        console.log(`Invitation code: ${created.code}`);
        console.log(
          `Uses: ${maxUses}. Expires: ${created.invitation.expiresAt ?? 'never'}. The code is shown only once; store it safely.`,
        );
        break;
      }

      case 'reset-password': {
        const onlineId = values['online-id']?.trim();
        if (!onlineId) fail('Provide --online-id.');
        const [user] = await database.db
          .select({ id: users.id, onlineId: users.onlineId })
          .from(users)
          .where(eq(users.onlineIdNormalized, onlineId.toLowerCase()));
        if (!user) fail(`No account with Online ID "${onlineId}".`);
        const password = await obtainPassword(user.onlineId, values['password-stdin'] === true);
        const passwordHash = await ctx.passwords.hash(password);
        await database.db.transaction(async (tx) => {
          await tx.update(users).set({ passwordHash, passwordChangedAt: new Date() }).where(eq(users.id, user.id));
          await tx.delete(authSessions).where(eq(authSessions.userId, user.id));
          await ctx.audit.record({ actorId: null, action: 'cli.password_reset', targetType: 'user', targetId: user.id }, tx);
        });
        console.log(`Password for "${user.onlineId}" was reset and all sessions were signed out.`);
        break;
      }

      case 'seed': {
        if (config.isProduction) fail('Refusing to seed demo data when NODE_ENV=production.');
        await seed(ctx, database.db);
        break;
      }

      default:
        console.log(HELP);
        fail(`Unknown command "${command}".`);
    }
  } finally {
    await database.close();
  }
}

async function seed(ctx: ReturnType<typeof createContext>, db: ReturnType<typeof createDatabase>['db']): Promise<void> {
  const password = process.env.SEED_PASSWORD ?? 'akeno-dev-password';
  const people = [
    { onlineId: 'alice', displayName: 'Alice' },
    { onlineId: 'bob', displayName: 'Bob' },
    { onlineId: 'carol', displayName: 'Carol' },
  ];
  const ids: Record<string, string> = {};
  for (const person of people) {
    const [existing] = await db.select({ id: users.id }).from(users).where(eq(users.onlineIdNormalized, person.onlineId));
    if (existing) {
      ids[person.onlineId] = existing.id;
      console.log(`- ${person.onlineId} already exists, skipping`);
      continue;
    }
    const created = await ctx.accounts.createAccount({ ...person, password, role: 'user' });
    ids[person.onlineId] = created.id;
    console.log(`- created ${person.onlineId}`);
  }
  const [low, high] = orderedPair(ids.alice!, ids.bob!);
  await db.insert(friendships).values({ userLowId: low, userHighId: high }).onConflictDoNothing();

  const [inGroup] = await db.select().from(groupMembers).where(eq(groupMembers.userId, ids.alice!)).limit(1);
  if (!inGroup) {
    const group = await ctx.groups.create(ids.alice!, {
      name: 'Night Raiders',
      description: 'Weekly co-op evenings. Demo group created by the seed script.',
      joinPolicy: 'open',
    });
    await ctx.groups.join(ids.bob!, group.id);
  }
  const invitation = await ctx.admin.createInvitation(null, { maxUses: 5, expiresInHours: 168, note: 'Development seed' }, null);

  console.log('\nDevelopment data is ready.');
  console.log(`Sign in as alice, bob or carol with the password "${password === 'akeno-dev-password' ? password : '<SEED_PASSWORD>'}".`);
  console.log(`Invitation code for new registrations: ${invitation.code}`);
  console.log('Never run the seed command against a production database.');
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? `Error: ${error.message}` : error);
  process.exit(1);
});
