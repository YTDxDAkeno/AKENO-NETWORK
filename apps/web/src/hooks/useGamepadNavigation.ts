import { useEffect } from 'react';

const FOCUSABLE =
  'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

function focusables(): HTMLElement[] {
  const dialog = document.querySelector('dialog[open]');
  const root: ParentNode = dialog ?? document;
  return Array.from(root.querySelectorAll<HTMLElement>(FOCUSABLE)).filter(
    (element) => element.offsetParent !== null || element === document.activeElement,
  );
}

function moveFocus(step: 1 | -1): void {
  const list = focusables();
  if (list.length === 0) return;
  const index = list.indexOf(document.activeElement as HTMLElement);
  const next = list[(index + step + list.length) % list.length];
  next?.focus();
  next?.scrollIntoView({ block: 'nearest' });
}

/**
 * Experimental controller support via the standard Gamepad API (opt-in in Settings).
 * D-pad / left stick moves focus, A activates, B goes back or closes a dialog.
 * This only reads controller input inside the browser tab; it never talks to a console.
 */
export function useGamepadNavigation(enabled: boolean): void {
  useEffect(() => {
    if (!enabled || typeof navigator.getGamepads !== 'function') return;
    let frame = 0;
    const previous = new Map<number, boolean>();
    let lastAxisMove = 0;

    const pressed = (pad: Gamepad, index: number) => Boolean(pad.buttons[index]?.pressed);
    const edge = (pad: Gamepad, index: number) => {
      const key = pad.index * 100 + index;
      const now = pressed(pad, index);
      const before = previous.get(key) ?? false;
      previous.set(key, now);
      return now && !before;
    };

    const tick = () => {
      for (const pad of navigator.getGamepads()) {
        if (!pad) continue;
        if (edge(pad, 12) || edge(pad, 14)) moveFocus(-1); // D-pad up / left
        if (edge(pad, 13) || edge(pad, 15)) moveFocus(1); // D-pad down / right
        if (edge(pad, 0)) (document.activeElement as HTMLElement | null)?.click(); // A / Cross
        if (edge(pad, 1)) {
          const dialog = document.querySelector<HTMLDialogElement>('dialog[open]');
          if (dialog) dialog.dispatchEvent(new Event('cancel'));
          else window.history.back();
        }
        const vertical = pad.axes[1] ?? 0;
        const now = performance.now();
        if (Math.abs(vertical) > 0.6 && now - lastAxisMove > 220) {
          lastAxisMove = now;
          moveFocus(vertical > 0 ? 1 : -1);
        }
      }
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [enabled]);
}
