import type * as LiveKitModule from 'livekit-client';
import type { LocalAudioTrack, RemoteParticipant, RemoteTrack, Room, Track, TrackProcessor } from 'livekit-client';
import type { VoiceTokenResponse } from '@akeno/shared';
import { ApiError, api } from '../../lib/api';
import { MicProcessor } from './mic-processor';
import { captureOptions, loadVoicePreferences, onVoicePreferences, type VoicePreferences } from './voice-preferences';

type LiveKit = typeof LiveKitModule;

export type VoiceStatus = 'idle' | 'requesting-microphone' | 'connecting' | 'connected' | 'reconnecting' | 'error';
export type VoiceQuality = 'excellent' | 'good' | 'poor' | 'lost' | 'unknown';

export interface VoiceParticipantState {
  identity: string;
  local: boolean;
  speaking: boolean;
  micMuted: boolean;
  quality: VoiceQuality;
}

export interface VoiceSnapshot {
  status: VoiceStatus;
  /** Party the user wants to be in voice with (also while reconnecting). */
  partyId: string | null;
  error: string | null;
  /** Non-fatal information (for example a microphone interruption). */
  notice: string | null;
  micMuted: boolean;
  deafened: boolean;
  pttHeld: boolean;
  /** The microphone gate is open (open mode, voice detected, or push-to-talk held). */
  transmitting: boolean;
  micLevel: number;
  canPlaybackAudio: boolean;
  quality: VoiceQuality;
  reconnectAttempt: number;
  participants: Record<string, VoiceParticipantState>;
}

const IDLE: VoiceSnapshot = {
  status: 'idle',
  partyId: null,
  error: null,
  notice: null,
  micMuted: false,
  deafened: false,
  pttHeld: false,
  transmitting: false,
  micLevel: 0,
  canPlaybackAudio: true,
  quality: 'unknown',
  reconnectAttempt: 0,
  participants: {},
};

const MAX_RECONNECTS = 8;

/** English, actionable text for microphone failures. */
export function microphoneErrorMessage(error: unknown): string {
  const name = (error as { name?: string })?.name ?? '';
  if (name === 'NotAllowedError' || name === 'SecurityError' || name === 'PermissionDeniedError') {
    return 'Microphone access was blocked. Allow the microphone for this site in your browser settings, then try again.';
  }
  if (name === 'NotFoundError' || name === 'OverconstrainedError' || name === 'DevicesNotFoundError') {
    return 'No microphone was found. Connect one, or choose another input in Settings → Voice & Audio.';
  }
  if (name === 'NotReadableError' || name === 'TrackStartError' || name === 'AbortError') {
    return 'The microphone could not be started. It may be in use by another app.';
  }
  return 'The microphone could not be started.';
}

function qualityOf(value: string | undefined): VoiceQuality {
  return value === 'excellent' || value === 'good' || value === 'poor' || value === 'lost' ? value : 'unknown';
}

/**
 * Owns the LiveKit room for the signed-in user's party voice chat. The AKENO server authorizes
 * every join (short-lived token) and can remove the user at any time; this class reflects that
 * honestly (kicked, party closed, voice moved to another tab) and only reconnects on its own
 * after network problems. livekit-client is loaded on first use, so pages without voice never
 * download it.
 */
export class VoiceController {
  private snapshot: VoiceSnapshot = IDLE;
  private readonly listeners = new Set<() => void>();
  private lk: LiveKit | null = null;
  private room: Room | null = null;
  private track: LocalAudioTrack | null = null;
  private processor: MicProcessor | null = null;
  private audioContext: AudioContext | null = null;
  private readonly audioElements = new Map<string, HTMLMediaElement>();
  private prefs: VoicePreferences = loadVoicePreferences();
  private reconnectTimer: number | null = null;
  private generation = 0;
  private readonly offPrefs: () => void;
  private readonly onOnline = () => {
    if (this.reconnectTimer !== null && this.snapshot.partyId) {
      window.clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
      void this.reconnect(this.generation);
    }
  };
  private readonly onVisible = () => {
    if (document.visibilityState === 'visible' && this.track?.mediaStreamTrack.readyState === 'ended') void this.resumeMicrophone();
  };

  constructor(private readonly onRemoved: (partyId: string) => void = () => {}) {
    this.offPrefs = onVoicePreferences((prefs) => this.applyPreferences(prefs));
    window.addEventListener('online', this.onOnline);
    document.addEventListener('visibilitychange', this.onVisible);
  }

  // ---- Store --------------------------------------------------------------------------------

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getSnapshot = (): VoiceSnapshot => this.snapshot;

  private set(patch: Partial<VoiceSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) listener();
  }

  // ---- Joining and leaving ----------------------------------------------------------------

  /** Must be called from a user gesture (click): it asks for the microphone and starts audio. */
  async join(partyId: string): Promise<void> {
    if (this.snapshot.partyId === partyId && (this.snapshot.status === 'connected' || this.snapshot.status === 'connecting')) return;
    await this.teardown();
    const generation = ++this.generation;
    if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia) {
      this.set({ ...IDLE, status: 'error', error: 'Voice chat needs a secure (HTTPS) connection and a browser with microphone support.' });
      return;
    }
    // Created inside the click so browsers allow audio to start (mobile Safari in particular).
    this.audioContext = new AudioContext({ latencyHint: 'interactive' });
    void this.audioContext.resume().catch(() => {});
    this.set({ ...IDLE, status: 'requesting-microphone', partyId, micMuted: this.snapshot.micMuted });
    try {
      this.lk ??= await import('livekit-client');
      this.track = await this.lk.createLocalAudioTrack(captureOptions(this.prefs));
    } catch (error) {
      if (generation !== this.generation) return;
      await this.teardown();
      this.set({ ...IDLE, status: 'error', partyId: null, error: microphoneErrorMessage(error) });
      return;
    }
    if (generation !== this.generation) return;
    this.watchMicrophone(this.track);
    await this.connect(generation);
  }

  private async connect(generation: number): Promise<void> {
    const partyId = this.snapshot.partyId;
    const lk = this.lk;
    if (!partyId || !lk || !this.track) return;
    this.set({ status: this.snapshot.reconnectAttempt > 0 ? 'reconnecting' : 'connecting', error: null });
    let grant: VoiceTokenResponse;
    try {
      grant = await api.post<VoiceTokenResponse>(`/parties/${partyId}/voice/token`);
    } catch (error) {
      if (generation !== this.generation) return;
      // Membership problems are final; server or network problems are retried.
      if (error instanceof ApiError && error.status > 0 && error.status < 500) return this.fail(error.message);
      return this.scheduleReconnect(generation, error instanceof Error ? error.message : 'The voice server is not reachable.');
    }
    if (generation !== this.generation) return;

    const room = new lk.Room({
      adaptiveStream: false,
      dynacast: false,
      // Remote audio plays through <audio> elements, not Web Audio: Chrome's echo canceller only
      // hears audio played that way, so people on speakers would otherwise echo to the party.
      webAudioMix: false,
      disconnectOnPageLeave: true,
      stopLocalTrackOnUnpublish: true,
      audioCaptureDefaults: captureOptions(this.prefs),
      ...(this.prefs.outputDeviceId ? { audioOutput: { deviceId: this.prefs.outputDeviceId } } : {}),
    });
    this.room = room;
    this.bindRoom(room, lk, generation);
    try {
      await room.connect(grant.url, grant.token, { autoSubscribe: true });
      if (generation !== this.generation) return;
      // A reconnect reuses the microphone: replace any processor from the previous connection.
      await this.processor?.destroy().catch(() => {});
      if (this.track.getProcessor()) await this.track.stopProcessor();
      if (this.audioContext) this.track.setAudioContext(this.audioContext);
      this.processor = new MicProcessor({ gain: this.prefs.inputGain, mode: this.prefs.mode, sensitivity: this.prefs.sensitivity, pttOpen: this.snapshot.pttHeld });
      this.processor.onLevel = (level, open) => this.onMicLevel(level, open);
      await this.track.setProcessor(this.processor as unknown as TrackProcessor<Track.Kind.Audio>);
      await room.localParticipant.publishTrack(this.track, { source: lk.Track.Source.Microphone, dtx: true, red: true });
      if (this.snapshot.micMuted || this.snapshot.deafened) await this.track.mute();
      await room.startAudio().catch(() => {});
    } catch (error) {
      if (generation !== this.generation) return;
      room.removeAllListeners();
      await room.disconnect(false).catch(() => {});
      this.room = null;
      return this.scheduleReconnect(generation, error instanceof Error ? error.message : 'Could not connect to the voice server.');
    }
    this.set({ status: 'connected', reconnectAttempt: 0, canPlaybackAudio: room.canPlaybackAudio, error: null });
    this.refreshParticipants();
  }

  private bindRoom(room: Room, lk: LiveKit, generation: number): void {
    const { RoomEvent } = lk;
    const refresh = () => this.refreshParticipants();
    room
      .on(RoomEvent.ParticipantConnected, refresh)
      .on(RoomEvent.TrackPublished, (_publication, participant: RemoteParticipant) => this.applyVolume(participant))
      .on(RoomEvent.ParticipantDisconnected, (participant: RemoteParticipant) => {
        this.audioElements.get(participant.identity)?.remove();
        this.audioElements.delete(participant.identity);
        refresh();
      })
      .on(RoomEvent.ActiveSpeakersChanged, refresh)
      .on(RoomEvent.TrackMuted, refresh)
      .on(RoomEvent.TrackUnmuted, refresh)
      .on(RoomEvent.ConnectionQualityChanged, refresh)
      .on(RoomEvent.TrackSubscribed, (track: RemoteTrack, _publication, participant: RemoteParticipant) => {
        if (track.kind !== lk.Track.Kind.Audio) return;
        const element = track.attach();
        element.dataset.akenoVoice = participant.identity;
        element.style.display = 'none';
        document.body.appendChild(element);
        this.audioElements.get(participant.identity)?.remove();
        this.audioElements.set(participant.identity, element);
        this.applyVolume(participant);
        refresh();
      })
      .on(RoomEvent.TrackUnsubscribed, (track: RemoteTrack) => {
        for (const element of track.detach()) element.remove();
        refresh();
      })
      .on(RoomEvent.AudioPlaybackStatusChanged, () => this.set({ canPlaybackAudio: room.canPlaybackAudio }))
      .on(RoomEvent.Reconnecting, () => this.set({ status: 'reconnecting', notice: 'Connection lost. Reconnecting…' }))
      .on(RoomEvent.Reconnected, () => {
        this.set({ status: 'connected', notice: null });
        refresh();
      })
      .on(RoomEvent.MediaDevicesError, (error: Error) => this.set({ notice: microphoneErrorMessage(error) }))
      .on(RoomEvent.Disconnected, (reason?: number) => {
        if (generation !== this.generation) return;
        void this.handleDisconnect(reason ?? 0, generation);
      });
  }

  private async handleDisconnect(reason: number, generation: number): Promise<void> {
    const R = this.lk?.DisconnectReason;
    const partyId = this.snapshot.partyId;
    this.room = null;
    if (!R || reason === R.CLIENT_INITIATED) return;
    if (reason === R.DUPLICATE_IDENTITY) return this.fail('Voice moved to another tab or device where you joined this party’s voice chat.');
    if (reason === R.PARTICIPANT_REMOVED) {
      if (partyId) this.onRemoved(partyId);
      return this.fail('You were disconnected from voice (you left or were removed from the party).');
    }
    if (reason === R.ROOM_DELETED) {
      if (partyId) this.onRemoved(partyId);
      return this.fail('The party’s voice chat ended.');
    }
    // Network loss, server restart, join failure: get a fresh token and reconnect.
    this.scheduleReconnect(generation, 'Connection to the voice server was lost.');
  }

  private scheduleReconnect(generation: number, message: string): void {
    const attempt = this.snapshot.reconnectAttempt + 1;
    if (attempt > MAX_RECONNECTS) return void this.fail(`${message} Try joining again.`);
    const delay = Math.min(30_000, 1000 * 2 ** (attempt - 1)) * (0.8 + Math.random() * 0.4);
    this.set({ status: 'reconnecting', reconnectAttempt: attempt, notice: `${message} Reconnecting (attempt ${attempt} of ${MAX_RECONNECTS})…` });
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null;
      void this.reconnect(generation);
    }, delay);
  }

  private async reconnect(generation: number): Promise<void> {
    if (generation !== this.generation || !this.snapshot.partyId) return;
    if (!this.track || this.track.mediaStreamTrack.readyState === 'ended') {
      try {
        this.track = await this.lk!.createLocalAudioTrack(captureOptions(this.prefs));
        this.watchMicrophone(this.track);
      } catch (error) {
        return this.fail(microphoneErrorMessage(error));
      }
    }
    await this.connect(generation);
  }

  private async fail(message: string): Promise<void> {
    await this.teardown();
    this.set({ ...IDLE, status: 'error', error: message });
  }

  /** Leaves voice and releases the microphone. */
  async leave(): Promise<void> {
    this.generation += 1;
    await this.teardown();
    this.set({ ...IDLE });
  }

  /** Closes everything: room, microphone (all tracks stopped), audio elements, timers. */
  private async teardown(): Promise<void> {
    if (this.reconnectTimer !== null) window.clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
    const room = this.room;
    this.room = null;
    if (room) {
      room.removeAllListeners();
      await room.disconnect(true).catch(() => {});
    }
    await this.track?.stopProcessor().catch(() => {});
    this.track?.stop();
    this.track?.mediaStreamTrack.stop();
    this.track = null;
    await this.processor?.destroy().catch(() => {});
    this.processor = null;
    for (const element of this.audioElements.values()) element.remove();
    this.audioElements.clear();
    await this.audioContext?.close().catch(() => {});
    this.audioContext = null;
  }

  dispose(): void {
    void this.leave();
    this.offPrefs();
    window.removeEventListener('online', this.onOnline);
    document.removeEventListener('visibilitychange', this.onVisible);
  }

  // ---- Controls -----------------------------------------------------------------------------

  async setMicMuted(muted: boolean): Promise<void> {
    this.set({ micMuted: muted });
    if (!this.track || this.snapshot.deafened) return;
    await (muted ? this.track.mute() : this.track.unmute());
    this.refreshParticipants();
  }

  /** Deafen: you hear nobody and your microphone is muted. */
  async setDeafened(deafened: boolean): Promise<void> {
    this.set({ deafened });
    for (const participant of this.room?.remoteParticipants.values() ?? []) this.applyVolume(participant);
    if (this.track) await (deafened || this.snapshot.micMuted ? this.track.mute() : this.track.unmute());
    this.refreshParticipants();
  }

  setPushToTalk(held: boolean): void {
    if (this.snapshot.pttHeld === held) return;
    this.processor?.update({ pttOpen: held });
    this.set({ pttHeld: held });
  }

  /** After an autoplay block (common on phones) audio starts on the next tap. */
  async startAudio(): Promise<void> {
    await this.audioContext?.resume().catch(() => {});
    await this.room?.startAudio().catch(() => {});
    if (this.room) this.set({ canPlaybackAudio: this.room.canPlaybackAudio });
  }

  /** A phone call or another app took the microphone: get it back. */
  async resumeMicrophone(): Promise<void> {
    if (!this.track) return;
    try {
      await this.track.restartTrack(captureOptions(this.prefs));
      this.watchMicrophone(this.track);
      this.set({ notice: null });
    } catch (error) {
      this.set({ notice: microphoneErrorMessage(error) });
    }
  }

  private watchMicrophone(track: LocalAudioTrack): void {
    track.mediaStreamTrack.addEventListener(
      'ended',
      () => {
        if (this.track !== track || this.snapshot.status === 'idle') return;
        this.set({ notice: 'Your microphone was interrupted (for example by a call). Tap “Resume microphone” to continue.' });
      },
      { once: true },
    );
  }

  private onMicLevel(level: number, open: boolean): void {
    const transmitting = open && !this.snapshot.micMuted && !this.snapshot.deafened;
    // Avoid re-rendering 20 times a second for tiny changes.
    if (Math.abs(level - this.snapshot.micLevel) > 0.04 || transmitting !== this.snapshot.transmitting) this.set({ micLevel: level, transmitting });
  }

  async switchDevice(kind: 'audioinput' | 'audiooutput', deviceId: string): Promise<boolean> {
    if (!this.room) return true;
    try {
      return await this.room.switchActiveDevice(kind, deviceId);
    } catch (error) {
      this.set({ notice: kind === 'audioinput' ? microphoneErrorMessage(error) : 'This browser cannot switch the speaker.' });
      return false;
    }
  }

  private applyPreferences(prefs: VoicePreferences): void {
    const previous = this.prefs;
    this.prefs = prefs;
    this.processor?.update({ gain: prefs.inputGain, mode: prefs.mode, sensitivity: prefs.sensitivity });
    for (const participant of this.room?.remoteParticipants.values() ?? []) this.applyVolume(participant);
    if (!this.track || !this.room) return;
    if (prefs.inputDeviceId !== previous.inputDeviceId && prefs.inputDeviceId) void this.switchDevice('audioinput', prefs.inputDeviceId);
    if (prefs.outputDeviceId !== previous.outputDeviceId && prefs.outputDeviceId) void this.switchDevice('audiooutput', prefs.outputDeviceId);
    if (
      prefs.echoCancellation !== previous.echoCancellation ||
      prefs.noiseSuppression !== previous.noiseSuppression ||
      prefs.autoGainControl !== previous.autoGainControl
    ) {
      void this.track.restartTrack(captureOptions(prefs)).catch((error: unknown) => this.set({ notice: microphoneErrorMessage(error) }));
    }
  }

  /**
   * Volume is 0–100% (media element volume; iPhone and iPad browsers ignore it). Muting someone
   * for yourself, or deafening, unsubscribes from their microphone instead: that works on every
   * browser and stops the audio from being sent to you at all.
   */
  private applyVolume(participant: RemoteParticipant): void {
    const muted = this.snapshot.deafened || Boolean(this.prefs.localMutes[participant.identity]);
    const publication = participant.getTrackPublication(this.lk!.Track.Source.Microphone);
    if (publication && publication.isDesired === muted) publication.setSubscribed(!muted);
    const volume = (this.prefs.userVolumes[participant.identity] ?? 1) * this.prefs.outputVolume;
    participant.setVolume(Math.min(1, Math.max(0, Number.isFinite(volume) ? volume : 1)));
  }

  private refreshParticipants(): void {
    const room = this.room;
    if (!room) return;
    const participants: Record<string, VoiceParticipantState> = {};
    const all = [room.localParticipant, ...room.remoteParticipants.values()];
    for (const participant of all) {
      const mic = participant.getTrackPublication(this.lk!.Track.Source.Microphone);
      participants[participant.identity] = {
        identity: participant.identity,
        local: participant === room.localParticipant,
        speaking: participant.isSpeaking,
        micMuted: !mic || mic.isMuted,
        quality: qualityOf(participant.connectionQuality),
      };
    }
    this.set({ participants, quality: qualityOf(room.localParticipant.connectionQuality) });
  }
}
