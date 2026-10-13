import { useEffect, useRef } from 'react';

const IDLE_AFTER_MS = 5 * 60_000;
const EVENTS = ['pointerdown', 'pointermove', 'keydown', 'wheel', 'touchstart', 'focus'] as const;

/** Reports idle after five minutes without input or while the tab stays hidden that long. */
export function useIdleDetection(onChange: (idle: boolean) => void, enabled: boolean): void {
  const callback = useRef(onChange);
  useEffect(() => {
    callback.current = onChange;
  }, [onChange]);

  useEffect(() => {
    if (!enabled) return;
    let idle = false;
    let timer = 0;
    const setIdle = (value: boolean) => {
      if (idle === value) return;
      idle = value;
      callback.current(value);
    };
    const arm = () => {
      window.clearTimeout(timer);
      timer = window.setTimeout(() => setIdle(true), IDLE_AFTER_MS);
    };
    let lastActivity = 0;
    const onActivity = () => {
      const now = Date.now();
      if (now - lastActivity < 1000 && !idle) return;
      lastActivity = now;
      setIdle(false);
      arm();
    };
    for (const name of EVENTS) window.addEventListener(name, onActivity, { passive: true });
    arm();
    return () => {
      window.clearTimeout(timer);
      for (const name of EVENTS) window.removeEventListener(name, onActivity);
    };
  }, [enabled]);
}
