/**
 * Per-device conveniences stored in localStorage. Every access is wrapped because storage can
 * be unavailable (private browsing, blocked site data). Nothing security-relevant lives here.
 */
export type Theme = 'dark' | 'light';

const THEME_KEY = 'akeno.theme';
const CONTROLLER_KEY = 'akeno.controllerNavigation';

function read(key: string): string | null {
  try {
    return window.localStorage.getItem(key);
  } catch {
    return null;
  }
}

function write(key: string, value: string): void {
  try {
    window.localStorage.setItem(key, value);
  } catch {
    // Ignore unavailable storage; the preference simply will not persist.
  }
}

export function getTheme(): Theme {
  return read(THEME_KEY) === 'light' ? 'light' : 'dark';
}

export function applyTheme(theme: Theme): void {
  document.documentElement.dataset.theme = theme;
  document.querySelector('meta[name="theme-color"]')?.setAttribute('content', theme === 'light' ? '#f2f4fa' : '#060912');
}

export function setTheme(theme: Theme): void {
  write(THEME_KEY, theme);
  applyTheme(theme);
}

export function getControllerNavigation(): boolean {
  return read(CONTROLLER_KEY) === 'true';
}

export function setControllerNavigation(enabled: boolean): void {
  write(CONTROLLER_KEY, String(enabled));
  window.dispatchEvent(new CustomEvent('akeno:preferences'));
}
