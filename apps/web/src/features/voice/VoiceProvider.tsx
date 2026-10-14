import { useQueryClient } from '@tanstack/react-query';
import { createContext, useContext, useEffect, useMemo, useState, useSyncExternalStore, type ReactNode } from 'react';
import { useAuth } from '../auth/AuthProvider';
import { queryKeys } from '../../lib/query-keys';
import { VoiceController, type VoiceSnapshot } from './voice-controller';
import { loadVoicePreferences, onVoicePreferences } from './voice-preferences';

const VoiceContext = createContext<VoiceController | null>(null);

function isTyping(target: EventTarget | null): boolean {
  const element = target as HTMLElement | null;
  return Boolean(element && (element.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(element.tagName)));
}

/**
 * One voice connection per signed-in user, kept across page navigation. Signing out (or the
 * session ending) leaves voice and releases the microphone.
 */
export function VoiceProvider({ children }: { children: ReactNode }) {
  const queryClient = useQueryClient();
  const { user } = useAuth();
  const userId = user?.id ?? null;
  const controller = useMemo(
    () =>
      userId
        ? new VoiceController(() => {
            void queryClient.invalidateQueries({ queryKey: queryKeys.allParties });
            void queryClient.invalidateQueries({ queryKey: queryKeys.joinableParties });
          })
        : null,
    [userId, queryClient],
  );
  useEffect(() => () => controller?.dispose(), [controller]);

  // Push-to-talk on desktop keyboards (only while this page has focus; browsers offer no global hotkeys).
  const [pttKey, setPttKey] = useState(() => loadVoicePreferences().pttKey);
  useEffect(() => onVoicePreferences((prefs) => setPttKey(prefs.pttKey)), []);
  useEffect(() => {
    if (!controller) return;
    const down = (event: KeyboardEvent) => {
      if (event.code !== pttKey || event.repeat || isTyping(event.target)) return;
      if (controller.getSnapshot().status === 'idle') return;
      controller.setPushToTalk(true);
    };
    const up = (event: KeyboardEvent) => {
      if (event.code === pttKey) controller.setPushToTalk(false);
    };
    const release = () => controller.setPushToTalk(false);
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    window.addEventListener('blur', release);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
      window.removeEventListener('blur', release);
    };
  }, [controller, pttKey]);

  return <VoiceContext.Provider value={controller}>{children}</VoiceContext.Provider>;
}

const EMPTY_SUBSCRIBE = () => () => {};

/** The voice controller (null while signed out). */
export function useVoiceController(): VoiceController | null {
  return useContext(VoiceContext);
}

/** Live voice state (re-renders on changes). */
export function useVoice(): VoiceSnapshot | null {
  const controller = useContext(VoiceContext);
  return useSyncExternalStore(controller?.subscribe ?? EMPTY_SUBSCRIBE, () => controller?.getSnapshot() ?? null);
}
