import type { WebSocket } from 'ws';
import {
  INPUT_HEARTBEAT_MS,
  INPUT_WATCHDOG_MS,
  InputRateLimiter,
  SHAREPLAY_INPUT_CLOSE,
  decodeInputPacket,
  type InputPacket,
  type SharePlayInputRelay,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { ControllerGrant, SharePlaySession, SharePlayService } from './service.js';

/** A receiver that is this far behind gets no further state packets until it catches up. */
const MAX_BUFFERED_BYTES = 16 * 1024;
/** Malformed or refused packets tolerated per connection before it is closed. */
const MAX_BAD_PACKETS = 50;
/** Rate-limited packets tolerated per connection before it is closed (a flood, not jitter). */
const MAX_FLOODED_PACKETS = 600;
const KEEPALIVE_MS = 15_000;

interface Sender {
  socket: WebSocket;
  partyId: string;
  userId: string;
  sessionId: string;
  grant: ControllerGrant;
  limiter: InputRateLimiter;
  bad: number;
  flooded: number;
  alive: boolean;
}

interface Monitor {
  socket: WebSocket;
  partyId: string;
  userId: string;
  alive: boolean;
}

function toBytes(raw: Buffer | ArrayBuffer | Buffer[]): Uint8Array {
  if (Array.isArray(raw)) return new Uint8Array(Buffer.concat(raw));
  if (raw instanceof ArrayBuffer) return new Uint8Array(raw);
  return new Uint8Array(raw.buffer, raw.byteOffset, raw.byteLength);
}

function close(socket: WebSocket, code: number, reason: string): void {
  try {
    socket.close(code, reason.slice(0, 120));
  } catch {
    socket.terminate();
  }
}

/**
 * Relays Player 2 controller packets: guest → AKENO server → the host's receivers. The server is
 * the enforcement point: every packet must belong to the party's current grant (same grant
 * object, same epoch), be well-formed, newer than the last accepted one and within the rate
 * limit. Anything else is dropped (and repeated abuse closes the connection).
 *
 * Receivers are (a) the host's browser monitor connections and (b) the host's paired console
 * app connections on the existing realtime link (event "shareplay.input"). Neither injects input
 * into a game; see docs/SHAREPLAY_STATUS.md.
 *
 * Old state is never queued behind new state: a receiver whose socket is backed up skips state
 * packets (the sender repeats its state every INPUT_HEARTBEAT_MS), while release packets are
 * always sent.
 */
export class InputRelay {
  private readonly senders = new Map<string, Sender>();
  private readonly monitors = new Map<string, Set<Monitor>>();
  private keepalive: NodeJS.Timeout | null = null;

  constructor(
    private readonly ctx: AppContext,
    private readonly service: SharePlayService,
  ) {}

  /** The browser session behind a user's open controller link, if any. */
  senderSessionOf(userId: string): string | null {
    for (const sender of this.senders.values()) if (sender.userId === userId) return sender.sessionId;
    return null;
  }

  stats(): { senders: number; monitors: number } {
    let monitors = 0;
    for (const set of this.monitors.values()) monitors += set.size;
    return { senders: this.senders.size, monitors };
  }

  private ensureKeepalive(): void {
    if (this.keepalive) return;
    this.keepalive = setInterval(() => {
      const sockets: Array<Sender | Monitor> = [...this.senders.values(), ...[...this.monitors.values()].flatMap((set) => [...set])];
      if (sockets.length === 0 && this.keepalive) {
        clearInterval(this.keepalive);
        this.keepalive = null;
        return;
      }
      for (const entry of sockets) {
        if (!entry.alive) {
          entry.socket.terminate();
          continue;
        }
        entry.alive = false;
        try {
          entry.socket.ping();
        } catch {
          entry.socket.terminate();
        }
      }
    }, KEEPALIVE_MS);
    this.keepalive.unref();
  }

  /** The guest's controller link (already authorized for `grant` by the route). */
  attachSender(socket: WebSocket, partyId: string, userId: string, sessionId: string, grant: ControllerGrant): void {
    const previous = this.senders.get(partyId);
    if (previous) {
      this.senders.delete(partyId);
      close(previous.socket, SHAREPLAY_INPUT_CLOSE.replaced, 'Controller connected from another tab or device.');
    }
    const sender: Sender = {
      socket,
      partyId,
      userId,
      sessionId,
      grant,
      limiter: new InputRateLimiter(),
      bad: 0,
      flooded: 0,
      alive: true,
    };
    this.senders.set(partyId, sender);
    this.ensureKeepalive();
    this.service.inputLinkChanged(partyId, grant, true);
    // The guest continues numbering after the last packet the server accepted for this grant.
    socket.send(JSON.stringify({ type: 'ready', epoch: grant.epoch, lastSeq: grant.sequencer.lastSeq, watchdogMs: INPUT_WATCHDOG_MS, heartbeatMs: INPUT_HEARTBEAT_MS }));

    socket.on('pong', () => {
      sender.alive = true;
    });
    socket.on('message', (raw: Buffer | ArrayBuffer | Buffer[], isBinary: boolean) => {
      sender.alive = true;
      this.onSenderPacket(sender, raw, isBinary);
    });
    socket.on('close', () => {
      if (this.senders.get(partyId) !== sender) return;
      this.senders.delete(partyId);
      const session = this.service.sessionOf(partyId);
      if (session?.grant === grant) {
        // The guest is gone (for now): everything is released at once, the grant waits briefly.
        this.forward(session, grant, this.releaseOf(session, grant, 'Player 2’s connection dropped.'), true);
        this.service.inputLinkChanged(partyId, grant, false);
      }
    });
    socket.on('error', (error) => this.ctx.log.debug({ err: error }, 'SharePlay controller socket error'));
  }

  private onSenderPacket(sender: Sender, raw: Buffer | ArrayBuffer | Buffer[], isBinary: boolean): void {
    const session = this.service.sessionOf(sender.partyId);
    const grant = session?.grant;
    if (!session || !grant || grant !== sender.grant) {
      close(sender.socket, SHAREPLAY_INPUT_CLOSE.grantEnded, 'Player 2 access has ended.');
      return;
    }
    if (!isBinary) {
      close(sender.socket, SHAREPLAY_INPUT_CLOSE.protocol, 'Controller packets must be binary.');
      return;
    }
    if (!sender.limiter.take()) {
      grant.rejected += 1;
      if (++sender.flooded > MAX_FLOODED_PACKETS) close(sender.socket, SHAREPLAY_INPUT_CLOSE.protocol, 'Too many controller packets.');
      return;
    }
    const decoded = decodeInputPacket(toBytes(raw));
    if (!decoded.ok || decoded.packet.epoch !== grant.epoch) {
      grant.rejected += 1;
      if (++sender.bad > MAX_BAD_PACKETS) close(sender.socket, SHAREPLAY_INPUT_CLOSE.protocol, 'Too many invalid controller packets.');
      return;
    }
    const packet = decoded.packet;
    if (!grant.sequencer.accept(packet.epoch, packet.seq)) {
      grant.rejected += 1; // stale or duplicated
      return;
    }
    grant.accepted += 1;
    grant.lastInputAt = Date.now();
    this.forward(session, grant, this.relayOf(session, grant, packet), packet.kind === 'release');
  }

  private relayOf(session: SharePlaySession, grant: ControllerGrant, packet: InputPacket): SharePlayInputRelay {
    return {
      sessionId: session.id,
      grantId: grant.grantId,
      epoch: grant.epoch,
      guestId: grant.guestId,
      slot: packet.slot,
      seq: packet.seq,
      kind: packet.kind,
      buttons: packet.buttons,
      axes: packet.axes,
      triggers: packet.triggers,
      senderTime: packet.senderTime,
      serverTime: Date.now(),
    };
  }

  /**
   * A server-generated release (it has a reason). Receivers apply releases at once, whatever
   * their sequence number: going to neutral is always safe, and the next state packet of a
   * grant that continues brings the input back.
   */
  private releaseOf(session: SharePlaySession, grant: ControllerGrant, reason: string): SharePlayInputRelay {
    return {
      sessionId: session.id,
      grantId: grant.grantId,
      epoch: grant.epoch,
      guestId: grant.guestId,
      slot: grant.slot,
      seq: grant.sequencer.lastSeq,
      kind: 'release',
      buttons: 0,
      axes: [0, 0, 0, 0],
      triggers: [0, 0],
      senderTime: 0,
      serverTime: Date.now(),
      reason,
    };
  }

  private forward(session: SharePlaySession, _grant: ControllerGrant, relay: SharePlayInputRelay, always: boolean): void {
    const text = JSON.stringify(relay);
    for (const monitor of this.monitors.get(session.partyId) ?? []) {
      if (monitor.socket.readyState !== monitor.socket.OPEN) continue;
      if (always || monitor.socket.bufferedAmount < MAX_BUFFERED_BYTES) monitor.socket.send(text);
    }
    for (const connection of this.ctx.hub.connectionsOf(session.hostId)) {
      if (connection.credential !== 'device') continue;
      if (always || connection.socket.bufferedAmount < MAX_BUFFERED_BYTES) connection.send({ type: 'shareplay.input', data: relay });
    }
  }

  /** The grant ended: receivers release everything, the guest's link closes. */
  grantEnded(session: SharePlaySession, grant: ControllerGrant, reason: string): void {
    this.forward(session, grant, this.releaseOf(session, grant, reason), true);
    const sender = this.senders.get(session.partyId);
    if (sender && sender.grant === grant) {
      this.senders.delete(session.partyId);
      close(sender.socket, SHAREPLAY_INPUT_CLOSE.grantEnded, reason);
    }
  }

  /** The host's own diagnostic view of Player 2's input. */
  attachMonitor(socket: WebSocket, partyId: string, userId: string): void {
    const set = this.monitors.get(partyId) ?? new Set<Monitor>();
    if (set.size >= 4) {
      close(socket, SHAREPLAY_INPUT_CLOSE.forbidden, 'Too many monitor connections.');
      return;
    }
    const monitor: Monitor = { socket, partyId, userId, alive: true };
    set.add(monitor);
    this.monitors.set(partyId, set);
    this.ensureKeepalive();
    socket.on('pong', () => {
      monitor.alive = true;
    });
    socket.on('message', () => close(socket, SHAREPLAY_INPUT_CLOSE.protocol, 'Monitors only receive.'));
    socket.on('close', () => {
      set.delete(monitor);
      if (set.size === 0 && this.monitors.get(partyId) === set) this.monitors.delete(partyId);
    });
    socket.on('error', (error) => this.ctx.log.debug({ err: error }, 'SharePlay monitor socket error'));
  }

  closeMonitors(partyId: string, reason: string): void {
    for (const monitor of this.monitors.get(partyId) ?? []) close(monitor.socket, SHAREPLAY_INPUT_CLOSE.grantEnded, reason);
    this.monitors.delete(partyId);
  }

  closeAll(): void {
    for (const sender of this.senders.values()) close(sender.socket, 1012, 'Server restarting');
    for (const set of this.monitors.values()) for (const monitor of set) close(monitor.socket, 1012, 'Server restarting');
    this.senders.clear();
    this.monitors.clear();
    if (this.keepalive) clearInterval(this.keepalive);
    this.keepalive = null;
  }
}
