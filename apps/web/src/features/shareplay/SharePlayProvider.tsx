import { useQueryClient } from '@tanstack/react-query';
import { createContext, useContext, useEffect, useMemo, useSyncExternalStore, type ReactNode } from 'react';
import { useAuth } from '../auth/AuthProvider';
import { useCurrentParty } from '../../lib/queries';
import { SHAREPLAY_EVENT, sharePlayKeys, type SharePlayEventDetail } from './queries';
import { PlayerTwoMonitor, PlayerTwoSender } from './player-two';
import { StreamController, type StreamSnapshot } from './stream-controller';

const StreamContext = createContext<StreamController | null>(null);

interface PlayerTwo {
  sender: PlayerTwoSender;
  monitor: PlayerTwoMonitor;
}
const PlayerTwoContext = createContext<PlayerTwo | null>(null);

/**
 * One SharePlay connection per signed-in user (broadcasting or watching), kept across page
 * navigation like voice. Signing out stops capture and leaves the stream.
 */
export function SharePlayProvider({ children }: { children: ReactNode }) {
  const queryClient = useQueryClient();
  const { user } = useAuth();
  const userId = user?.id ?? null;
  const controller = useMemo(
    () => (userId ? new StreamController((partyId) => void queryClient.invalidateQueries({ queryKey: sharePlayKeys.party(partyId) })) : null),
    [userId, queryClient],
  );
  useEffect(() => () => controller?.dispose(), [controller]);
  const playerTwo = useMemo<PlayerTwo>(() => ({ sender: new PlayerTwoSender(), monitor: new PlayerTwoMonitor() }), []);
  useEffect(() => {
    // Signing out (or switching accounts) ends any controller link at once.
    playerTwo.sender.stop();
    playerTwo.monitor.stop();
  }, [userId, playerTwo]);

  useEffect(() => {
    if (!controller) return;
    const onEvent = (event: Event) => {
      const detail = (event as CustomEvent<SharePlayEventDetail>).detail;
      if (detail.ended) void controller.sessionEnded(detail.partyId);
    };
    window.addEventListener(SHAREPLAY_EVENT, onEvent);
    return () => window.removeEventListener(SHAREPLAY_EVENT, onEvent);
  }, [controller]);

  return (
    <StreamContext.Provider value={controller}>
      <PlayerTwoContext.Provider value={playerTwo}>{children}</PlayerTwoContext.Provider>
    </StreamContext.Provider>
  );
}

export function usePlayerTwo(): PlayerTwo {
  const value = useContext(PlayerTwoContext);
  if (!value) throw new Error('usePlayerTwo outside SharePlayProvider');
  return value;
}

const EMPTY_SUBSCRIBE = () => () => {};

export function useStreamController(): StreamController | null {
  return useContext(StreamContext);
}

export function useStream(): StreamSnapshot | null {
  const controller = useContext(StreamContext);
  return useSyncExternalStore(controller?.subscribe ?? EMPTY_SUBSCRIBE, () => controller?.getSnapshot() ?? null);
}

/** Active (capturing, connecting, live, waiting or reconnecting). */
export function isStreamActive(stream: StreamSnapshot | null): boolean {
  return Boolean(stream && stream.partyId && !['idle', 'ended', 'error'].includes(stream.status));
}

/** Leaves the stream (and stops capture) when the user is no longer in that party. */
export function SharePlayPartyWatcher() {
  const controller = useStreamController();
  const stream = useStream();
  const party = useCurrentParty();
  const streamPartyId = isStreamActive(stream) ? stream!.partyId : null;
  const currentId = party.data?.id ?? null;
  const loaded = party.isSuccess && !party.isFetching;
  useEffect(() => {
    if (streamPartyId && loaded && currentId !== streamPartyId) void controller?.leave();
  }, [streamPartyId, currentId, loaded, controller]);
  return null;
}
