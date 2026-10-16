import { createHash } from 'node:crypto';
import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import type { AddressInfo } from 'node:net';
import type { FastifyInstance } from 'fastify';
import { AccessToken } from 'livekit-server-sdk';

/**
 * A stand-in for LiveKit's room service API (Twirp/JSON) that records what AKENO asks for, plus a
 * signer for LiveKit-style webhooks. The real SFU is exercised by the browser tests
 * (apps/web/tests/e2e/voice.spec.ts and shareplay.spec.ts).
 */
export class FakeLiveKit {
  readonly rooms = new Map<string, { maxParticipants: number; participants: Set<string> }>();
  readonly calls: Array<{ method: string; body: any; authorization: string | undefined }> = [];
  server!: Server;
  url!: string;

  constructor(
    readonly apiKey: string,
    readonly apiSecret: string,
  ) {}

  async start(): Promise<void> {
    this.server = createServer((request, response) => void this.handle(request, response));
    await new Promise<void>((resolve) => this.server.listen(0, '127.0.0.1', resolve));
    this.url = `ws://127.0.0.1:${(this.server.address() as AddressInfo).port}`;
  }

  stop(): Promise<void> {
    return new Promise((resolve) => this.server.close(() => resolve()));
  }

  private async handle(request: IncomingMessage, response: ServerResponse): Promise<void> {
    let raw = '';
    for await (const chunk of request) raw += chunk;
    const body = raw ? JSON.parse(raw) : {};
    const method = (request.url ?? '').split('/').pop() ?? '';
    this.calls.push({ method, body, authorization: request.headers.authorization });
    const json = (status: number, data: unknown) => {
      response.writeHead(status, { 'content-type': 'application/json' });
      response.end(JSON.stringify(data));
    };
    switch (method) {
      case 'CreateRoom': {
        const room = this.rooms.get(body.name) ?? { maxParticipants: body.maxParticipants ?? 0, participants: new Set<string>() };
        this.rooms.set(body.name, room);
        return json(200, { name: body.name, sid: `RM_${body.name}`, maxParticipants: room.maxParticipants });
      }
      case 'ListRooms':
        return json(200, { rooms: [...this.rooms.keys()].map((name) => ({ name, sid: `RM_${name}` })) });
      case 'DeleteRoom':
        if (!this.rooms.delete(body.room)) return json(404, { code: 'not_found', msg: 'room not found' });
        return json(200, {});
      case 'ListParticipants':
        return json(200, { participants: [...(this.rooms.get(body.room)?.participants ?? [])].map((identity) => ({ identity, sid: `PA_${identity}` })) });
      case 'RemoveParticipant': {
        const room = this.rooms.get(body.room);
        if (!room?.participants.delete(body.identity)) return json(404, { code: 'not_found', msg: 'participant not found' });
        return json(200, {});
      }
      default:
        return json(404, { code: 'bad_route', msg: 'unknown method' });
    }
  }

  removed(identity: string, room?: string): boolean {
    return this.calls.some((call) => call.method === 'RemoveParticipant' && call.body.identity === identity && (!room || call.body.room === room));
  }

  deleted(room: string): boolean {
    return this.calls.some((call) => call.method === 'DeleteRoom' && call.body.room === room);
  }

  /** Sends a webhook signed like LiveKit does (JWT with the body's SHA-256), optionally forged. */
  async webhook(app: FastifyInstance, event: Record<string, unknown>, options: { secret?: string } = {}) {
    const body = JSON.stringify({ id: `EV_${Math.random()}`, createdAt: String(Math.floor(Date.now() / 1000)), ...event });
    const token = new AccessToken(this.apiKey, options.secret ?? this.apiSecret);
    token.sha256 = createHash('sha256').update(body).digest('base64');
    const jwt = await token.toJwt();
    return app.inject({
      method: 'POST',
      url: '/api/v1/voice/webhook',
      headers: { 'content-type': 'application/webhook+json', authorization: jwt },
      payload: body,
    });
  }

  /** Simulates a participant connecting to the SFU, which then reports the join. */
  async join(app: FastifyInstance, room: string, identity: string) {
    const entry = this.rooms.get(room) ?? { maxParticipants: 0, participants: new Set<string>() };
    entry.participants.add(identity);
    this.rooms.set(room, entry);
    return this.webhook(app, { event: 'participant_joined', room: { name: room, sid: `RM_${room}` }, participant: { identity, sid: `PA_${identity}` } });
  }

  async leave(app: FastifyInstance, room: string, identity: string) {
    this.rooms.get(room)?.participants.delete(identity);
    return this.webhook(app, { event: 'participant_left', room: { name: room }, participant: { identity } });
  }

  /** The host publishes (or unpublishes) its screen-share video. */
  async track(app: FastifyInstance, room: string, identity: string, published = true, source = 'SCREEN_SHARE') {
    return this.webhook(app, {
      event: published ? 'track_published' : 'track_unpublished',
      room: { name: room },
      participant: { identity },
      track: { sid: `TR_${identity}`, type: 'VIDEO', source },
    });
  }
}
