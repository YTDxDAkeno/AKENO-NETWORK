import type * as LiveKitModule from 'livekit-client';
import type { RemoteParticipant, RemoteTrack, RemoteTrackPublication, Room } from 'livekit-client';
import {
  SHAREPLAY_AUDIO_BITRATE,
  SHAREPLAY_QUALITY_PRESETS,
  type SharePlayQuality,
  type SharePlaySessionDto,
  type SharePlaySource,
  type SharePlayTokenResponse,
} from '@akeno/shared';
import { ApiError, api } from '../../lib/api';

type LiveKit = typeof LiveKitModule;
/** One entry of an RTCStatsReport (its fields depend on the entry type, so they are read loosely). */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type RtcStat = Record<string, any>;

export type StreamRole = 'host' | 'viewer';
export type StreamStatus = 'idle' | 'capturing' | 'connecting' | 'live' | 'waiting' | 'reconnecting' | 'ended' | 'error';
export type StreamQuality = 'excellent' | 'good' | 'poor' | 'lost' | 'unknown';

/** What the viewer's browser measures about the received stream (null: not measured yet). */
export interface ReceiveStats {
  width: number | null;
  height: number | null;
  framesPerSecond: number | null;
  /** Received video + audio bitrate in kbit/s. */
  bitrateKbps: number | null;
  /** Lost packets as a share of expected ones over the last interval, 0..1. */
  packetLoss: number | null;
  jitterMs: number | null;
  /** Average time frames waited in the receive buffer (jitter buffer), ms. */
  bufferMs: number | null;
  /** Round trip time between this browser and the media server, ms. */
  roundTripMs: number | null;
  framesDecoded: number;
  codec: string | null;
}

/** What the broadcaster's browser measures about the sent stream. */
export interface SendStats {
  width: number | null;
  height: number | null;
  framesPerSecond: number | null;
  bitrateKbps: number | null;
  /** Why the encoder lowers quality right now ("bandwidth", "cpu", none). */
  limitation: string | null;
  roundTripMs: number | null;
  codec: string | null;
}

export interface StreamSnapshot {
  status: StreamStatus;
  role: StreamRole | null;
  partyId: string | null;
  source: SharePlaySource | null;
  /** Human readable label of what is being captured (from the browser), for the capture indicator. */
  captureLabel: string | null;
  hasAudio: boolean;
  error: string | null;
  notice: string | null;
  quality: StreamQuality;
  /** Viewer: the host's video track is subscribed. */
  receiving: boolean;
  muted: boolean;
  volume: number;
  canPlayback: boolean;
  reconnectAttempt: number;
  receive: ReceiveStats | null;
  send: SendStats | null;
}

const IDLE: StreamSnapshot = {
  status: 'idle',
  role: null,
  partyId: null,
  source: null,
  captureLabel: null,
  hasAudio: false,
  error: null,
  notice: null,
  quality: 'unknown',
  receiving: false,
  muted: false,
  volume: 1,
  canPlayback: true,
  reconnectAttempt: 0,
  receive: null,
  send: null,
};

const MAX_RECONNECTS = 6;

/** Browser support for the broadcaster's capture sources. */
export function captureSupport(): Record<'screen' | 'capture-device', boolean> {
  const media = typeof navigator !== 'undefined' ? navigator.mediaDevices : undefined;
  const secure = typeof window !== 'undefined' && window.isSecureContext;
  return {
    screen: Boolean(secure && media && typeof media.getDisplayMedia === 'function'),
    'capture-device': Boolean(secure && media && typeof media.getUserMedia === 'function'),
  };
}

/** Watching needs WebRTC (missing in some console and embedded browsers). */
export function canWatchStreams(): boolean {
  return typeof window !== 'undefined' && typeof window.RTCPeerConnection === 'function';
}

export function captureErrorMessage(error: unknown, source: SharePlaySource): string {
  const name = (error as { name?: string })?.name ?? '';
  if (name === 'NotAllowedError' || name === 'AbortError') {
    return source === 'screen' ? 'Sharing was cancelled. Nothing was streamed.' : 'Access to the capture device was blocked. Allow the camera for this site to use a capture card.';
  }
  if (name === 'NotFoundError' || name === 'OverconstrainedError') return 'The selected capture device was not found. Is it connected?';
  if (name === 'NotReadableError') return 'The capture device is in use by another app (close OBS or the capture card’s own app).';
  return 'Capture could not start.';
}

function qualityOf(value: string | undefined): StreamQuality {
  return value === 'excellent' || value === 'good' || value === 'poor' || value === 'lost' ? value : 'unknown';
}

/**
 * Owns the SharePlay room of the signed-in user (one at a time): broadcasting as host, or
 * watching as a viewer. This is a different LiveKit room from party voice, so a video failure
 * never ends the voice connection. Nothing is captured until the person explicitly picks a
 * source in the browser's own picker, and capture stops the moment they stop.
 */
export class StreamController {
  private snapshot: StreamSnapshot = IDLE;
  private readonly listeners = new Set<() => void>();
  private lk: LiveKit | null = null;
  private room: Room | null = null;
  private generation = 0;
  private reconnectTimer: number | null = null;
  private statsTimer: number | null = null;
  private captured: MediaStream | null = null;
  private quality: SharePlayQuality = '720p30';
  private videoElement: HTMLVideoElement | null = null;
  private audioElement: HTMLAudioElement | null = null;
  private videoTrack: RemoteTrack | null = null;
  private previousStats: { at: number; bytes: number; frames: number; lost: number; received: number; jbDelay: number; jbEmitted: number } | null = null;
  private previousSend: { at: number; bytes: number } | null = null;
  private readonly onOnline = () => {
    if (this.reconnectTimer !== null && this.snapshot.partyId) {
      window.clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
      void this.connect(this.generation);
    }
  };

  constructor(
    /** Called when the server-side session changed because of this client (refetch). */
    private readonly onChanged: (partyId: string) => void = () => {},
  ) {
    window.addEventListener('online', this.onOnline);
  }

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getSnapshot = (): StreamSnapshot => this.snapshot;

  private set(patch: Partial<StreamSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) listener();
  }

  // ---- Broadcasting ---------------------------------------------------------------------------

  /**
   * Must be called from a click: opens the browser's own picker (screen) or the capture device,
   * then starts the session on the server and publishes. Cancelling the picker streams nothing.
   */
  async startBroadcast(partyId: string, source: Exclude<SharePlaySource, 'ps5-native'>, quality: SharePlayQuality, deviceId?: string): Promise<void> {
    await this.teardown();
    const generation = ++this.generation;
    this.quality = quality;
    this.set({ ...IDLE, status: 'capturing', role: 'host', partyId, source });
    let stream: MediaStream;
    try {
      stream = await this.capture(source, quality, deviceId);
    } catch (error) {
      if (generation !== this.generation) return;
      this.set({ ...IDLE, status: 'error', error: captureErrorMessage(error, source) });
      return;
    }
    if (generation !== this.generation) return stopStream(stream);
    this.captured = stream;
    const video = stream.getVideoTracks()[0]!;
    // Games are motion: keep the frame rate and lower resolution first under pressure.
    if ('contentHint' in video) video.contentHint = 'motion';
    video.addEventListener('ended', () => {
      // The browser's own "Stop sharing" button (or the device was unplugged).
      if (this.captured === stream) void this.stopBroadcast('Capture stopped.');
    });
    this.set({ captureLabel: video.label || null, hasAudio: stream.getAudioTracks().length > 0 });
    try {
      await api.post<SharePlaySessionDto>(`/parties/${partyId}/shareplay/start`, { source, quality });
    } catch (error) {
      stopStream(stream);
      this.captured = null;
      if (generation !== this.generation) return;
      this.set({ ...IDLE, status: 'error', error: error instanceof Error ? error.message : 'SharePlay could not start.' });
      return;
    }
    this.onChanged(partyId);
    await this.connect(generation);
  }

  private async capture(source: Exclude<SharePlaySource, 'ps5-native'>, quality: SharePlayQuality, deviceId?: string): Promise<MediaStream> {
    const preset = SHAREPLAY_QUALITY_PRESETS[quality];
    const video: MediaTrackConstraints = { width: { ideal: preset.width }, height: { ideal: preset.height }, frameRate: { ideal: preset.frameRate, max: preset.frameRate } };
    // Game audio: no voice processing (it would treat game sound as noise or echo).
    const audio: MediaTrackConstraints = { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2 };
    if (source === 'screen') {
      return navigator.mediaDevices.getDisplayMedia({
        video,
        audio,
        // Never offer the AKENO tab itself (party chat, private messages).
        selfBrowserSurface: 'exclude',
        surfaceSwitching: 'exclude',
        systemAudio: 'include',
        monitorTypeSurfaces: 'include',
      } as DisplayMediaStreamOptions);
    }
    const stream = await navigator.mediaDevices.getUserMedia({ video: { ...video, ...(deviceId ? { deviceId: { exact: deviceId } } : {}) } });
    // Capture cards expose their HDMI audio as a separate input with a similar name.
    const label = stream.getVideoTracks()[0]?.label ?? '';
    const inputs = (await navigator.mediaDevices.enumerateDevices()).filter((device) => device.kind === 'audioinput');
    const match = inputs.find((input) => label && input.label && (input.label.includes(label.split(' (')[0]!) || label.includes(input.label.split(' (')[0]!)));
    if (match) {
      try {
        const sound = await navigator.mediaDevices.getUserMedia({ audio: { ...audio, deviceId: { exact: match.deviceId } } });
        for (const track of sound.getAudioTracks()) stream.addTrack(track);
      } catch {
        this.set({ notice: 'The capture device’s audio could not be opened; streaming video only.' });
      }
    }
    return stream;
  }

  async stopBroadcast(reason?: string): Promise<void> {
    const partyId = this.snapshot.partyId;
    const wasHost = this.snapshot.role === 'host';
    this.generation += 1;
    await this.teardown();
    this.set({ ...IDLE, ...(reason ? { status: 'ended' as const, notice: reason } : {}) });
    if (wasHost && partyId) {
      await api.post(`/parties/${partyId}/shareplay/stop`).catch(() => {});
      this.onChanged(partyId);
    }
  }

  /** Changes the encoder targets (and the server's record of the quality). */
  async setQuality(quality: SharePlayQuality): Promise<void> {
    const partyId = this.snapshot.partyId;
    if (this.snapshot.role !== 'host' || !partyId) return;
    this.quality = quality;
    const preset = SHAREPLAY_QUALITY_PRESETS[quality];
    const video = this.captured?.getVideoTracks()[0];
    await video?.applyConstraints({ width: { ideal: preset.width }, height: { ideal: preset.height }, frameRate: { ideal: preset.frameRate, max: preset.frameRate } }).catch(() => {});
    await api.post(`/parties/${partyId}/shareplay/quality`, { quality });
    if (this.room && this.lk && video) {
      // New encodings need a new publication.
      const publication = this.room.localParticipant.getTrackPublication(this.lk.Track.Source.ScreenShare);
      if (publication?.track) await this.room.localParticipant.unpublishTrack(publication.track, false);
      await this.publishVideo(video);
    }
  }

  private async publishVideo(video: MediaStreamTrack): Promise<void> {
    const lk = this.lk!;
    const preset = SHAREPLAY_QUALITY_PRESETS[this.quality];
    await this.room!.localParticipant.publishTrack(video, {
      source: lk.Track.Source.ScreenShare,
      name: 'shareplay-video',
      // H.264 is hardware-encoded on most Windows PCs; VP8 as backup for browsers without it.
      videoCodec: 'h264',
      backupCodec: { codec: 'vp8' },
      simulcast: preset.lowLayer !== null,
      screenShareEncoding: { maxBitrate: preset.maxBitrate, maxFramerate: preset.frameRate },
      ...(preset.lowLayer
        ? { screenShareSimulcastLayers: [new lk.VideoPreset(preset.lowLayer.width, preset.lowLayer.height, preset.lowLayer.maxBitrate, preset.lowLayer.frameRate)] }
        : {}),
      degradationPreference: 'maintain-framerate',
    });
  }

  // ---- Watching -------------------------------------------------------------------------------

  async watch(partyId: string): Promise<void> {
    if (this.snapshot.partyId === partyId && this.snapshot.role === 'viewer' && this.snapshot.status !== 'error' && this.snapshot.status !== 'ended') return;
    await this.teardown();
    const generation = ++this.generation;
    this.set({ ...IDLE, status: 'connecting', role: 'viewer', partyId, muted: this.snapshot.muted, volume: this.snapshot.volume });
    await this.connect(generation);
  }

  async leave(): Promise<void> {
    if (this.snapshot.role === 'host') return this.stopBroadcast();
    this.generation += 1;
    await this.teardown();
    this.set({ ...IDLE, muted: this.snapshot.muted, volume: this.snapshot.volume });
  }

  /** The party page hands over its <video> element (and takes it back on unmount). */
  attachVideo(element: HTMLVideoElement | null): void {
    if (this.videoElement && this.videoTrack) this.videoTrack.detach(this.videoElement);
    this.videoElement = element;
    if (element && this.videoTrack) this.videoTrack.attach(element);
  }

  setVolume(volume: number): void {
    const value = Math.min(1, Math.max(0, volume));
    if (this.audioElement) this.audioElement.volume = value;
    this.set({ volume: value });
  }

  setMuted(muted: boolean): void {
    if (this.audioElement) this.audioElement.muted = muted;
    this.set({ muted });
  }

  async startPlayback(): Promise<void> {
    await this.room?.startAudio().catch(() => {});
    await this.audioElement?.play().catch(() => {});
    await this.videoElement?.play().catch(() => {});
    if (this.room) this.set({ canPlayback: this.room.canPlaybackAudio });
  }

  // ---- Connection -----------------------------------------------------------------------------

  private async connect(generation: number): Promise<void> {
    const partyId = this.snapshot.partyId;
    if (!partyId) return;
    this.set({ status: this.snapshot.reconnectAttempt > 0 ? 'reconnecting' : 'connecting', error: null });
    let grant: SharePlayTokenResponse;
    try {
      this.lk ??= await import('livekit-client');
      grant = await api.post<SharePlayTokenResponse>(`/parties/${partyId}/shareplay/token`);
    } catch (error) {
      if (generation !== this.generation) return;
      if (error instanceof ApiError && error.status === 404) return this.end('The stream has ended.');
      if (error instanceof ApiError && error.status > 0 && error.status < 500) return this.fail(error.message);
      return this.scheduleReconnect(generation, error instanceof Error ? error.message : 'The streaming server is not reachable.');
    }
    if (generation !== this.generation) return;
    const lk = this.lk;
    const host = grant.role === 'host';
    const room = new lk.Room({
      // Viewers get the simulcast layer that fits their player and network.
      adaptiveStream: !host,
      dynacast: host,
      webAudioMix: false,
      disconnectOnPageLeave: true,
      stopLocalTrackOnUnpublish: false,
    });
    this.room = room;
    this.bindRoom(room, lk, generation);
    try {
      await room.connect(grant.url, grant.token, { autoSubscribe: !host });
      if (generation !== this.generation) return;
      if (host) {
        const video = this.captured?.getVideoTracks()[0];
        if (!video || video.readyState === 'ended') return this.stopBroadcast('Capture stopped.');
        await this.publishVideo(video);
        const audio = this.captured?.getAudioTracks()[0];
        if (audio) {
          await room.localParticipant.publishTrack(audio, {
            source: lk.Track.Source.ScreenShareAudio,
            name: 'shareplay-audio',
            audioPreset: { maxBitrate: SHAREPLAY_AUDIO_BITRATE },
            dtx: false,
            red: false,
            forceStereo: true,
          });
        }
      }
    } catch (error) {
      if (generation !== this.generation) return;
      room.removeAllListeners();
      await room.disconnect(false).catch(() => {});
      this.room = null;
      return this.scheduleReconnect(generation, error instanceof Error ? error.message : 'Could not connect to the streaming server.');
    }
    this.set({ status: host ? 'live' : this.videoTrack ? 'live' : 'waiting', reconnectAttempt: 0, notice: null, canPlayback: room.canPlaybackAudio });
    if (host) this.onChanged(partyId);
    this.startStats();
    if (!host) this.pickUpExistingTracks(room, lk);
  }

  private pickUpExistingTracks(room: Room, lk: LiveKit): void {
    for (const participant of room.remoteParticipants.values()) {
      for (const publication of participant.trackPublications.values()) {
        if (publication.track) this.onTrack(publication.track as RemoteTrack, publication as RemoteTrackPublication, participant, lk);
      }
    }
  }

  private bindRoom(room: Room, lk: LiveKit, generation: number): void {
    const { RoomEvent } = lk;
    room
      .on(RoomEvent.TrackSubscribed, (track: RemoteTrack, publication: RemoteTrackPublication, participant: RemoteParticipant) =>
        this.onTrack(track, publication, participant, lk),
      )
      .on(RoomEvent.TrackUnsubscribed, (track: RemoteTrack) => {
        if (track === this.videoTrack) {
          if (this.videoElement) track.detach(this.videoElement);
          this.videoTrack = null;
          this.set({ receiving: false, status: 'waiting', receive: null });
        } else {
          for (const element of track.detach()) element.remove();
          if (this.audioElement && track.kind === lk.Track.Kind.Audio) this.audioElement = null;
          this.set({ hasAudio: false });
        }
      })
      .on(RoomEvent.ParticipantDisconnected, () => this.set({ notice: this.snapshot.role === 'viewer' ? 'The host’s connection dropped. Waiting for the stream…' : null }))
      .on(RoomEvent.ConnectionQualityChanged, () => this.set({ quality: qualityOf(room.localParticipant.connectionQuality) }))
      .on(RoomEvent.AudioPlaybackStatusChanged, () => this.set({ canPlayback: room.canPlaybackAudio }))
      .on(RoomEvent.Reconnecting, () => this.set({ status: 'reconnecting', notice: 'Connection lost. Reconnecting…' }))
      .on(RoomEvent.Reconnected, () => this.set({ status: this.snapshot.role === 'host' || this.videoTrack ? 'live' : 'waiting', notice: null }))
      .on(RoomEvent.Disconnected, (reason?: number) => {
        if (generation !== this.generation) return;
        void this.handleDisconnect(reason ?? 0, generation);
      });
  }

  private onTrack(track: RemoteTrack, publication: RemoteTrackPublication, _participant: RemoteParticipant, lk: LiveKit): void {
    if (publication.source === lk.Track.Source.ScreenShare && track.kind === lk.Track.Kind.Video) {
      this.videoTrack = track;
      if (this.videoElement) track.attach(this.videoElement);
      this.previousStats = null;
      this.set({ receiving: true, status: 'live', notice: null });
    } else if (publication.source === lk.Track.Source.ScreenShareAudio && track.kind === lk.Track.Kind.Audio) {
      const element = track.attach() as HTMLAudioElement;
      element.dataset.akenoShareplay = 'audio';
      element.style.display = 'none';
      element.volume = this.snapshot.volume;
      element.muted = this.snapshot.muted;
      document.body.appendChild(element);
      this.audioElement?.remove();
      this.audioElement = element;
      this.set({ hasAudio: true });
    }
  }

  private async handleDisconnect(reason: number, generation: number): Promise<void> {
    const R = this.lk?.DisconnectReason;
    this.room = null;
    if (!R || reason === R.CLIENT_INITIATED) return;
    if (reason === R.DUPLICATE_IDENTITY) return this.fail('SharePlay moved to another tab or device.');
    if (reason === R.PARTICIPANT_REMOVED) return this.end('You were removed from the stream.');
    if (reason === R.ROOM_DELETED) return this.end('The stream has ended.');
    this.scheduleReconnect(generation, 'Connection to the streaming server was lost.');
  }

  private scheduleReconnect(generation: number, message: string): void {
    const attempt = this.snapshot.reconnectAttempt + 1;
    if (attempt > MAX_RECONNECTS) return void this.fail(`${message} Try again.`);
    const delay = Math.min(15_000, 1000 * 2 ** (attempt - 1)) * (0.8 + Math.random() * 0.4);
    this.set({ status: 'reconnecting', reconnectAttempt: attempt, notice: `${message} Reconnecting (attempt ${attempt} of ${MAX_RECONNECTS})…` });
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null;
      if (generation === this.generation) void this.connect(generation);
    }, delay);
  }

  /** Server says the session is over (or we were removed). */
  private async end(message: string): Promise<void> {
    const partyId = this.snapshot.partyId;
    this.generation += 1;
    await this.teardown();
    this.set({ ...IDLE, status: 'ended', notice: message, muted: this.snapshot.muted, volume: this.snapshot.volume });
    if (partyId) this.onChanged(partyId);
  }

  private async fail(message: string): Promise<void> {
    await this.teardown();
    this.set({ ...IDLE, status: 'error', error: message });
  }

  /** The server reports the session ended (realtime): stop capturing / watching at once. */
  async sessionEnded(partyId: string): Promise<void> {
    if (this.snapshot.partyId !== partyId || this.snapshot.status === 'idle') return;
    await this.end('The stream has ended.');
  }

  /** Stops capture (all tracks), leaves the room, removes media elements and timers. */
  private async teardown(): Promise<void> {
    if (this.reconnectTimer !== null) window.clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
    this.stopStats();
    const room = this.room;
    this.room = null;
    if (room) {
      room.removeAllListeners();
      await room.disconnect(true).catch(() => {});
    }
    if (this.captured) stopStream(this.captured);
    this.captured = null;
    if (this.videoTrack && this.videoElement) this.videoTrack.detach(this.videoElement);
    if (this.videoElement) this.videoElement.srcObject = null;
    this.videoTrack = null;
    this.audioElement?.remove();
    this.audioElement = null;
    this.previousStats = null;
    this.previousSend = null;
  }

  dispose(): void {
    void this.leave();
    window.removeEventListener('online', this.onOnline);
  }

  // ---- Statistics -----------------------------------------------------------------------------

  private startStats(): void {
    this.stopStats();
    this.statsTimer = window.setInterval(() => void this.collectStats(), 1000);
  }

  private stopStats(): void {
    if (this.statsTimer !== null) window.clearInterval(this.statsTimer);
    this.statsTimer = null;
  }

  private async collectStats(): Promise<void> {
    if (this.snapshot.role === 'viewer') return this.collectReceiveStats();
    if (this.snapshot.role === 'host') return this.collectSendStats();
  }

  private async collectReceiveStats(): Promise<void> {
    const track = this.videoTrack;
    const report = await track?.getRTCStatsReport().catch(() => undefined);
    if (!track || !report) return;
    let inbound: RtcStat | null = null;
    let pair: RtcStat | null = null;
    const codecs = new Map<string, string>();
    report.forEach((entry: RtcStat) => {
      if (entry.type === 'inbound-rtp' && entry.kind === 'video') inbound = entry;
      if (entry.type === 'candidate-pair' && entry.nominated && entry.state === 'succeeded') pair = entry;
      if (entry.type === 'codec') codecs.set(entry.id, String(entry.mimeType ?? '').replace(/^video\//, ''));
    });
    if (!inbound) return;
    const v = inbound as RtcStat;
    const now = performance.now();
    const current = {
      at: now,
      bytes: Number(v.bytesReceived ?? 0),
      frames: Number(v.framesDecoded ?? 0),
      lost: Number(v.packetsLost ?? 0),
      received: Number(v.packetsReceived ?? 0),
      jbDelay: Number(v.jitterBufferDelay ?? 0),
      jbEmitted: Number(v.jitterBufferEmittedCount ?? 0),
    };
    const previous = this.previousStats;
    this.previousStats = current;
    const seconds = previous ? (current.at - previous.at) / 1000 : 0;
    const expected = previous ? current.received - previous.received + (current.lost - previous.lost) : 0;
    const emitted = previous ? current.jbEmitted - previous.jbEmitted : 0;
    const p = pair as RtcStat | null;
    this.set({
      receive: {
        width: v.frameWidth ?? null,
        height: v.frameHeight ?? null,
        framesPerSecond: v.framesPerSecond ?? (previous && seconds > 0 ? Math.round((current.frames - previous.frames) / seconds) : null),
        bitrateKbps: previous && seconds > 0 ? Math.round(((current.bytes - previous.bytes) * 8) / seconds / 1000) : null,
        packetLoss: previous && expected > 0 ? Math.max(0, (current.lost - previous.lost) / expected) : null,
        jitterMs: typeof v.jitter === 'number' ? Math.round(v.jitter * 1000) : null,
        bufferMs: previous && emitted > 0 ? Math.round(((current.jbDelay - previous.jbDelay) / emitted) * 1000) : null,
        roundTripMs: p && typeof p.currentRoundTripTime === 'number' ? Math.round(p.currentRoundTripTime * 1000) : null,
        framesDecoded: current.frames,
        codec: codecs.get(v.codecId) ?? null,
      },
    });
  }

  private async collectSendStats(): Promise<void> {
    const lk = this.lk;
    const publication = lk ? this.room?.localParticipant.getTrackPublication(lk.Track.Source.ScreenShare) : undefined;
    const report = await publication?.track?.getRTCStatsReport().catch(() => undefined);
    if (!report) return;
    let bytes = 0;
    let best: RtcStat | null = null;
    let pair: RtcStat | null = null;
    const codecs = new Map<string, string>();
    report.forEach((entry: RtcStat) => {
      if (entry.type === 'outbound-rtp' && entry.kind === 'video') {
        bytes += Number(entry.bytesSent ?? 0);
        if (!best || Number(entry.frameWidth ?? 0) > Number(best.frameWidth ?? 0)) best = entry;
      }
      if (entry.type === 'candidate-pair' && entry.nominated && entry.state === 'succeeded') pair = entry;
      if (entry.type === 'codec') codecs.set(entry.id, String(entry.mimeType ?? '').replace(/^video\//, ''));
    });
    const now = performance.now();
    const previous = this.previousSend;
    this.previousSend = { at: now, bytes };
    const b = best as RtcStat | null;
    const p = pair as RtcStat | null;
    const seconds = previous ? (now - previous.at) / 1000 : 0;
    this.set({
      send: {
        width: b?.frameWidth ?? null,
        height: b?.frameHeight ?? null,
        framesPerSecond: b?.framesPerSecond ?? null,
        bitrateKbps: previous && seconds > 0 ? Math.round(((bytes - previous.bytes) * 8) / seconds / 1000) : null,
        limitation: b?.qualityLimitationReason && b.qualityLimitationReason !== 'none' ? String(b.qualityLimitationReason) : null,
        roundTripMs: p && typeof p.currentRoundTripTime === 'number' ? Math.round(p.currentRoundTripTime * 1000) : null,
        codec: b ? (codecs.get(b.codecId) ?? null) : null,
      },
    });
  }
}

function stopStream(stream: MediaStream): void {
  for (const track of stream.getTracks()) track.stop();
}
