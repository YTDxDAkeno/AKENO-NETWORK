import type { LanPlayConnectionMode, LanPlayPlatform } from '@akeno/shared';

/**
 * The explicit seam between the AKENO LAN Play **control plane** (this server: parties, roles,
 * authorization, credentials, revocation) and a **data plane** that actually carries a game's LAN
 * traffic. A future, independently verified and supported networking component implements this
 * interface; the party system never changes to accommodate it.
 *
 * Nothing in this repository implements a packet-forwarding data plane. The only adapter wired in
 * this build is `UnavailableTransport`, which authorizes nothing and reports
 * "Transport not available". That keeps every claim honest: LAN Play can create, manage and tear
 * down sessions, but it will not pretend a connection exists until a real transport is connected
 * here and verified with hardware.
 */

export interface TransportParticipant {
  userId: string;
  platform: LanPlayPlatform;
  role: 'host' | 'player';
}

export interface TransportSession {
  id: string;
  partyId: string;
  participants: TransportParticipant[];
}

/** What an adapter returns when it authorizes a participant to connect. */
export interface TransportGrant {
  connectionMode: LanPlayConnectionMode;
  connectable: boolean;
  /** Non-secret connection hints handed to the participant (endpoints, relay address, …). */
  details: Record<string, string>;
}

export interface TransportAvailability {
  available: boolean;
  reason: string | null;
}

export interface LanTransportAdapter {
  /** Stable identifier shown in status, e.g. "none". */
  readonly id: string;
  readonly label: string;
  /** Whether this adapter can currently authorize real connections. */
  availability(): TransportAvailability;
  /**
   * Authorize one participant of a session to connect. Returns the connection descriptor, or
   * `null` when the adapter cannot carry traffic (the control plane then reports the reason and
   * issues no credential). Must never throw for the unavailable case.
   */
  authorize(session: TransportSession, participant: TransportParticipant): Promise<TransportGrant | null>;
  /** Tear down any state the adapter holds for a participant (revocation, leave). */
  revoke(sessionId: string, userId: string): Promise<void>;
  /** Tear down all state for a session (termination, party close). */
  release(sessionId: string): Promise<void>;
}

export const TRANSPORT_NOT_AVAILABLE =
  'Transport not available: AKENO LAN Play has no verified network transport connected yet. Sessions can be created and managed, but no game traffic is carried. See docs/LAN_PLAY_FEASIBILITY.md.';

/** The built-in adapter: authorizes nothing, carries nothing. The production default. */
export class UnavailableTransport implements LanTransportAdapter {
  readonly id = 'none';
  readonly label = 'No transport connected';

  availability(): TransportAvailability {
    return { available: false, reason: TRANSPORT_NOT_AVAILABLE };
  }

  async authorize(): Promise<TransportGrant | null> {
    return null;
  }

  async revoke(): Promise<void> {}

  async release(): Promise<void> {}
}
