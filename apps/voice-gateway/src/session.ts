import {
  AudioFrame,
  AudioSource,
  AudioStream,
  DisconnectReason,
  LocalAudioTrack,
  type LocalTrackPublication,
  type RemoteParticipant,
  type RemoteTrack,
  Room,
  RoomEvent,
  TrackKind,
  TrackPublishOptions,
  TrackSource,
} from '@livekit/rtc-node';
import { TokenVerifier } from 'livekit-server-sdk';
import OpusScript from 'opusscript';
import type { Logger } from 'pino';
import type { WebSocket } from 'ws';
import type { GatewayConfig } from './config.js';
import { levelOf, Mixer } from './mixer.js';
import {
  CLOSE,
  FRAME_SAMPLES,
  MAX_OPUS_PACKET,
  packAudio,
  parseClientMessage,
  PROTOCOL_VERSION,
  type ParticipantView,
  SAMPLE_RATE,
  unpackAudio,
} from './protocol.js';

/** Participant attribute the AKENO server puts into console voice passes (apps/server voice service). */
export const CONSOLE_ATTRIBUTE = 'akeno.client';
export const CONSOLE_VALUE = 'console-gateway';
const ROOM_PATTERN = /^akeno-party-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

const HELLO_TIMEOUT_MS = 5_000;
const IDLE_TIMEOUT_MS = 30_000;
const SPEAKING_LEVEL = 20; // about -48 dBFS on the 0..100 meter
const MAX_UPLINK_PER_SECOND = 75; // 50 expected
const MAX_PENDING_CAPTURE = 25; // 500 ms
const MAX_BUFFERED_BYTES = 64 * 1024;

export interface SessionRegistry {
  sessions: Map<string, Session>;
  maxSessions: number;
}

/**
 * One console in one party's voice room. The console talks Opus over the WebSocket; the gateway
 * is a LiveKit participant with the console user's identity (the AKENO server issued the token),
 * so the other members see and hear that user exactly as they would a browser.
 *
 *   console mic (Opus) --> decode --> AudioSource --> LiveKit (microphone track)
 *   LiveKit remote audio --> AudioStream per member --> Mixer --> encode --> console (Opus)
 *
 * Nothing is recorded, stored or inspected beyond levels for the speaking indicators.
 */
export class Session {
  private state: 'hello' | 'connecting' | 'live' | 'closed' = 'hello';
  private identity = '';
  private roomName = '';
  private room: Room | null = null;
  private muted = false;
  private deafened = false;
  private readonly mixer = new Mixer();
  private readonly encoder = new OpusScript(SAMPLE_RATE, 1, OpusScript.Application.VOIP);
  private readonly decoder = new OpusScript(SAMPLE_RATE, 1, OpusScript.Application.VOIP);
  private source: AudioSource | null = null;
  private track: LocalAudioTrack | null = null;
  private publication: LocalTrackPublication | null = null;
  private publishing: Promise<void> = Promise.resolve();
  private seq = 0;
  private pendingCapture = 0;
  private uplinkWindowStart = 0;
  private uplinkInWindow = 0;
  private selfLevel = 0;
  private lastMessageAt = Date.now();
  private tickTimer: NodeJS.Timeout | null = null;
  private tickStart = 0;
  private ticksDone = 0;
  private watchdog: NodeJS.Timeout | null = null;
  private lastView = '';
  private viewAt = 0;
  readonly stats = { up: 0, down: 0, droppedUp: 0, droppedDown: 0 };

  constructor(
    private readonly ws: WebSocket,
    private readonly config: GatewayConfig,
    private readonly log: Logger,
    private readonly registry: SessionRegistry,
  ) {
    this.encoder.setBitrate(32_000);
    ws.on('message', (data, isBinary) => {
      this.lastMessageAt = Date.now();
      const buffer = Array.isArray(data) ? Buffer.concat(data) : Buffer.isBuffer(data) ? data : Buffer.from(data as ArrayBuffer);
      if (isBinary) this.onAudio(buffer);
      else void this.onText(buffer.toString('utf8'));
    });
    ws.on('close', () => void this.shutdown(CLOSE.normal, 'socket closed', false));
    ws.on('error', () => void this.shutdown(CLOSE.normal, 'socket error', false));
    this.watchdog = setInterval(() => {
      const idle = Date.now() - this.lastMessageAt;
      if (this.state === 'hello' && idle > HELLO_TIMEOUT_MS) void this.shutdown(CLOSE.timeout, 'No hello received.');
      else if (idle > IDLE_TIMEOUT_MS) void this.shutdown(CLOSE.timeout, 'The console stopped answering.');
    }, 1_000);
  }

  private send(message: Record<string, unknown>): void {
    if (this.ws.readyState === this.ws.OPEN) this.ws.send(JSON.stringify(message));
  }

  private async onText(text: string): Promise<void> {
    const message = parseClientMessage(text);
    if (!message) return this.shutdown(CLOSE.protocol, 'Malformed message.');
    switch (message.type) {
      case 'hello':
        if (this.state !== 'hello') return this.shutdown(CLOSE.protocol, 'Unexpected hello.');
        return this.hello(message.token, message.protocol, message.muted, message.deafened, message.client);
      case 'mute':
        this.muted = message.muted;
        if (this.state === 'live') this.publishing = this.publishing.then(() => this.applyMute()).catch(() => undefined);
        return;
      case 'deafen':
        this.deafened = message.deafened;
        return;
      case 'ping':
        this.send({ type: 'pong', id: message.id });
        return;
      case 'leave':
        return this.shutdown(CLOSE.normal, 'Left voice.');
    }
  }

  private async hello(token: string, protocol: number, muted: boolean, deafened: boolean, client: string): Promise<void> {
    if (protocol !== PROTOCOL_VERSION) {
      this.send({ type: 'error', code: 'PROTOCOL', message: `This gateway speaks protocol ${PROTOCOL_VERSION}.` });
      return this.shutdown(CLOSE.protocol, 'Protocol version not supported.');
    }
    let claims;
    try {
      claims = await new TokenVerifier(this.config.apiKey, this.config.apiSecret).verify(token);
    } catch {
      this.send({ type: 'error', code: 'TOKEN_INVALID', message: 'The voice pass is not valid or has expired.' });
      return this.shutdown(CLOSE.auth, 'Invalid token.');
    }
    const room = claims.video?.room ?? '';
    if (!claims.sub || claims.video?.roomJoin !== true || !ROOM_PATTERN.test(room) || claims.attributes?.[CONSOLE_ATTRIBUTE] !== CONSOLE_VALUE) {
      this.send({ type: 'error', code: 'TOKEN_INVALID', message: 'This voice pass is not for console voice.' });
      return this.shutdown(CLOSE.auth, 'Token is not a console voice pass.');
    }
    this.identity = claims.sub;
    this.roomName = room;
    this.muted = muted;
    this.deafened = deafened;
    // One console connection per account: a newer one replaces the older (LiveKit would too).
    const previous = this.registry.sessions.get(this.identity);
    if (previous) await previous.shutdown(CLOSE.replaced, 'Joined voice again.');
    if (this.registry.sessions.size >= this.registry.maxSessions) {
      this.send({ type: 'error', code: 'GATEWAY_BUSY', message: 'The voice gateway is full right now.' });
      return this.shutdown(CLOSE.busy, 'Too many sessions.');
    }
    this.registry.sessions.set(this.identity, this);
    this.state = 'connecting';
    this.log.info({ identity: this.identity, room, client }, 'console voice session connecting');

    const lk = new Room();
    this.room = lk;
    lk.on(RoomEvent.TrackSubscribed, (track, _publication, participant) => this.onTrack(track, participant));
    lk.on(RoomEvent.TrackUnsubscribed, (_track, _publication, participant) => this.mixer.remove(participant.identity));
    lk.on(RoomEvent.ParticipantDisconnected, (participant) => this.mixer.remove(participant.identity));
    lk.on(RoomEvent.Reconnecting, () => this.send({ type: 'state', state: 'reconnecting' }));
    lk.on(RoomEvent.Reconnected, () => this.send({ type: 'state', state: 'connected' }));
    lk.on(RoomEvent.Disconnected, (reason) => {
      if (this.state === 'closed') return;
      const code =
        reason === DisconnectReason.DUPLICATE_IDENTITY
          ? CLOSE.replaced
          : reason === DisconnectReason.PARTICIPANT_REMOVED
            ? CLOSE.removed
            : reason === DisconnectReason.ROOM_DELETED
              ? CLOSE.ended
              : CLOSE.unavailable;
      void this.shutdown(code, `LiveKit disconnected (${DisconnectReason[reason] ?? reason}).`, true, false);
    });
    try {
      await lk.connect(this.config.livekitUrl, token, { autoSubscribe: true, dynacast: false });
    } catch (error) {
      this.log.warn({ identity: this.identity, err: String(error) }, 'LiveKit refused or unreachable');
      const refused = /401|403|unauthori|permission|not allowed|full/i.test(String(error));
      this.send({
        type: 'error',
        code: refused ? 'TOKEN_INVALID' : 'VOICE_SERVER_UNREACHABLE',
        message: refused ? 'The voice server refused this voice pass.' : 'The voice server is not reachable right now.',
      });
      return this.shutdown(refused ? CLOSE.auth : CLOSE.unavailable, 'LiveKit connect failed.', true, false);
    }
    if ((this.state as Session['state']) === 'closed') {
      await lk.disconnect().catch(() => undefined);
      return;
    }
    this.source = new AudioSource(SAMPLE_RATE, 1, 1000);
    this.state = 'live';
    await this.applyMute();
    for (const participant of lk.remoteParticipants.values()) {
      for (const publication of participant.trackPublications.values()) {
        if (publication.track && publication.kind === TrackKind.KIND_AUDIO) this.onTrack(publication.track as RemoteTrack, participant);
      }
    }
    this.send({ type: 'ready', protocol: PROTOCOL_VERSION, room, identity: this.identity, sampleRate: SAMPLE_RATE, channels: 1, frameMs: 20 });
    this.tickStart = performance.now();
    this.ticksDone = 0;
    this.tickTimer = setInterval(() => this.tick(), 5);
    this.log.info({ identity: this.identity, room }, 'console voice session live');
  }

  /** The console's microphone track exists exactly while it is not muted (others see "muted"). */
  private async applyMute(): Promise<void> {
    const room = this.room;
    if (!room?.localParticipant || !this.source || this.state !== 'live') return;
    if (this.muted && this.publication?.sid) {
      const sid = this.publication.sid;
      this.publication = null;
      await room.localParticipant.unpublishTrack(sid, false).catch(() => undefined);
      await this.track?.close(false).catch(() => undefined);
      this.track = null;
      this.source.clearQueue();
    } else if (!this.muted && !this.publication) {
      this.track = LocalAudioTrack.createAudioTrack('microphone', this.source);
      const options = new TrackPublishOptions();
      options.source = TrackSource.SOURCE_MICROPHONE;
      this.publication = await room.localParticipant.publishTrack(this.track, options);
    }
  }

  private onTrack(track: RemoteTrack, participant: RemoteParticipant): void {
    if (track.kind !== TrackKind.KIND_AUDIO) return;
    const id = participant.identity;
    this.mixer.add(id);
    const stream = new AudioStream(track, { sampleRate: SAMPLE_RATE, numChannels: 1, frameSizeMs: 20 });
    void (async () => {
      let pending = new Int16Array(0);
      try {
        for await (const frame of stream) {
          if (this.state === 'closed') break;
          const data = frame.data;
          const joined = new Int16Array(pending.length + data.length);
          joined.set(pending);
          joined.set(data, pending.length);
          let offset = 0;
          while (joined.length - offset >= FRAME_SAMPLES) {
            this.mixer.push(id, joined.slice(offset, offset + FRAME_SAMPLES));
            offset += FRAME_SAMPLES;
          }
          pending = joined.slice(offset);
        }
      } catch {
        /* the track went away */
      }
    })();
  }

  /** Runs on a 5 ms timer and produces one mixed 20 ms frame per 20 ms of wall-clock time. */
  private tick(): void {
    if (this.state !== 'live') return;
    const due = Math.floor((performance.now() - this.tickStart) / 20);
    let budget = 3;
    while (this.ticksDone < due && budget-- > 0) {
      this.ticksDone++;
      const mixed = this.mixer.tick();
      if (mixed && !this.deafened) this.sendFrame(mixed);
    }
    if (this.ticksDone < due) this.ticksDone = due; // never fall further behind
    this.maybeSendView();
  }

  private sendFrame(pcm: Int16Array): void {
    if (this.ws.bufferedAmount > MAX_BUFFERED_BYTES) {
      this.stats.droppedDown++;
      return;
    }
    const packet = this.encoder.encode(Buffer.from(pcm.buffer, pcm.byteOffset, pcm.byteLength), FRAME_SAMPLES);
    if (packet.length === 0 || packet.length > MAX_OPUS_PACKET) return;
    this.ws.send(packAudio(this.seq, packet), { binary: true });
    this.seq = (this.seq + 1) & 0xffff;
    this.stats.down++;
  }

  private onAudio(frame: Buffer): void {
    if (this.state !== 'live' || this.muted || !this.source) return;
    const now = Date.now();
    if (now - this.uplinkWindowStart >= 1000) {
      this.uplinkWindowStart = now;
      this.uplinkInWindow = 0;
    }
    if (++this.uplinkInWindow > MAX_UPLINK_PER_SECOND || this.pendingCapture > MAX_PENDING_CAPTURE) {
      this.stats.droppedUp++;
      return;
    }
    const audio = unpackAudio(frame);
    if (!audio) return void this.shutdown(CLOSE.protocol, 'Malformed audio frame.');
    let pcm: Buffer;
    try {
      pcm = this.decoder.decode(audio.opus);
    } catch {
      this.stats.droppedUp++;
      return;
    }
    if (pcm.length !== FRAME_SAMPLES * 2) {
      this.stats.droppedUp++;
      return;
    }
    const samples = new Int16Array(FRAME_SAMPLES);
    for (let i = 0; i < FRAME_SAMPLES; i++) samples[i] = pcm.readInt16LE(i * 2);
    this.selfLevel = levelOf(samples);
    this.pendingCapture++;
    this.source
      .captureFrame(new AudioFrame(samples, SAMPLE_RATE, 1, FRAME_SAMPLES))
      .catch(() => undefined)
      .finally(() => this.pendingCapture--);
    this.stats.up++;
  }

  private view(): ParticipantView[] {
    const room = this.room;
    if (!room) return [];
    const list: ParticipantView[] = [
      {
        id: this.identity,
        name: room.localParticipant?.name ?? '',
        muted: this.muted,
        speaking: !this.muted && this.selfLevel >= SPEAKING_LEVEL,
        level: this.muted ? 0 : this.selfLevel,
        self: true,
      },
    ];
    for (const participant of room.remoteParticipants.values()) {
      const audio = [...participant.trackPublications.values()].find((p) => p.kind === TrackKind.KIND_AUDIO);
      const muted = !audio || audio.muted === true;
      const level = muted ? 0 : this.mixer.level(participant.identity);
      list.push({ id: participant.identity, name: participant.name ?? '', muted, speaking: level >= SPEAKING_LEVEL, level, self: false });
    }
    return list.slice(0, 16);
  }

  /** Sends the participant list when it changed (levels at most every 200 ms). */
  private maybeSendView(): void {
    const now = Date.now();
    if (now - this.viewAt < 200) return;
    const list = this.view();
    const json = JSON.stringify(list);
    if (json === this.lastView && now - this.viewAt < 5_000) return;
    this.lastView = json;
    this.viewAt = now;
    this.send({ type: 'participants', list });
  }

  /** Ends the session. leaveRoom=false when LiveKit already ended it. */
  async shutdown(code: number, reason: string, notify = true, leaveRoom = true): Promise<void> {
    if (this.state === 'closed') return;
    const wasLive = this.state === 'live';
    this.state = 'closed';
    if (this.tickTimer) clearInterval(this.tickTimer);
    if (this.watchdog) clearInterval(this.watchdog);
    if (this.identity && this.registry.sessions.get(this.identity) === this) this.registry.sessions.delete(this.identity);
    if (notify && this.ws.readyState === this.ws.OPEN) this.ws.close(code, reason.slice(0, 100));
    const room = this.room;
    this.room = null;
    if (room && leaveRoom) await room.disconnect().catch(() => undefined);
    await this.track?.close(false).catch(() => undefined);
    await this.source?.close().catch(() => undefined);
    this.encoder.delete();
    this.decoder.delete();
    if (this.identity) {
      this.log.info({ identity: this.identity, room: this.roomName, code, live: wasLive, ...this.stats }, 'console voice session ended');
    }
  }
}
