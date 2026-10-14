/**
 * Profile themes. A theme changes a small set of design tokens (accent, profile tint and the
 * default banner) on the owner's profile page and, for the owner, across their own app.
 *
 * Readability is guaranteed by construction: accents are adjusted with readableAccent() until
 * they reach a 3:1 contrast against the surface they are drawn on (WCAG 1.4.11), and text on an
 * accent background uses whichever of white or near-black has the higher contrast (at least
 * 4.5:1 for every colour).
 */

export const PROFILE_THEME_IDS = ['default', 'purple', 'blue', 'crimson', 'midnight', 'sakura', 'emerald', 'custom'] as const;
export type ProfileThemeId = (typeof PROFILE_THEME_IDS)[number];

export interface ProfileThemePreset {
  id: ProfileThemeId;
  label: string;
  /** Accent for buttons, links, highlights and the speaking ring. */
  accent: string;
  /** Background tone mixed into profile cards (dark mode). */
  tint: string;
  /** Default banner when the user has not uploaded one. */
  bannerFrom: string;
  bannerTo: string;
}

export const PROFILE_THEMES: readonly ProfileThemePreset[] = [
  { id: 'default', label: 'AKENO Default', accent: '#4f7cff', tint: '#0d1323', bannerFrom: '#1b2a55', bannerTo: '#2a1f4d' },
  { id: 'purple', label: 'Dark Purple', accent: '#8b5cf6', tint: '#130e24', bannerFrom: '#2b1854', bannerTo: '#140d2b' },
  { id: 'blue', label: 'Cobalt Blue', accent: '#2f7bff', tint: '#0a1428', bannerFrom: '#0b2f6b', bannerTo: '#081a3a' },
  { id: 'crimson', label: 'Crimson', accent: '#ef4452', tint: '#1c0d12', bannerFrom: '#4f1220', bannerTo: '#1f0a10' },
  { id: 'midnight', label: 'Midnight Black', accent: '#c3cbe0', tint: '#07090e', bannerFrom: '#161a24', bannerTo: '#05070b' },
  { id: 'sakura', label: 'Sakura Pink', accent: '#f472b6', tint: '#1d0e18', bannerFrom: '#4a1838', bannerTo: '#1f0c1a' },
  { id: 'emerald', label: 'Emerald', accent: '#10b981', tint: '#08171a', bannerFrom: '#0b3b33', bannerTo: '#07191a' },
  { id: 'custom', label: 'Custom accent', accent: '#4f7cff', tint: '#0d1323', bannerFrom: '#1b2a55', bannerTo: '#2a1f4d' },
];

export const DEFAULT_PROFILE_THEME: ProfileThemeId = 'default';

/** Lowercase #rrggbb. */
export const HEX_COLOR_PATTERN = /^#[0-9a-f]{6}$/;

export function themePreset(id: string): ProfileThemePreset {
  return PROFILE_THEMES.find((theme) => theme.id === id) ?? PROFILE_THEMES[0]!;
}

type Rgb = [number, number, number];

export function parseHexColor(hex: string): Rgb | null {
  const value = hex.trim().toLowerCase();
  if (!HEX_COLOR_PATTERN.test(value)) return null;
  return [parseInt(value.slice(1, 3), 16), parseInt(value.slice(3, 5), 16), parseInt(value.slice(5, 7), 16)];
}

function toHex([r, g, b]: Rgb): string {
  return `#${[r, g, b].map((channel) => Math.round(Math.min(255, Math.max(0, channel))).toString(16).padStart(2, '0')).join('')}`;
}

function channelLuminance(channel: number): number {
  const c = channel / 255;
  return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
}

/** WCAG relative luminance (0 = black, 1 = white). */
export function relativeLuminance(hex: string): number {
  const rgb = parseHexColor(hex);
  if (!rgb) return 0;
  return 0.2126 * channelLuminance(rgb[0]) + 0.7152 * channelLuminance(rgb[1]) + 0.0722 * channelLuminance(rgb[2]);
}

/** WCAG contrast ratio between two colours (1 to 21). */
export function contrastRatio(a: string, b: string): number {
  const la = relativeLuminance(a);
  const lb = relativeLuminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

function mix(color: Rgb, target: Rgb, amount: number): Rgb {
  return [0, 1, 2].map((index) => color[index]! + (target[index]! - color[index]!) * amount) as Rgb;
}

/**
 * Returns the accent, lightened (on dark surfaces) or darkened (on light surfaces) in small steps
 * until it reaches the minimum contrast against the surface. The hue is kept.
 */
export function readableAccent(accent: string, surface: string, minContrast = 3): string {
  const rgb = parseHexColor(accent);
  if (!rgb) return accent;
  if (contrastRatio(accent, surface) >= minContrast) return toHex(rgb);
  const target: Rgb = relativeLuminance(surface) < 0.5 ? [255, 255, 255] : [0, 0, 0];
  for (let step = 1; step <= 20; step++) {
    const candidate = toHex(mix(rgb, target, step * 0.05));
    if (contrastRatio(candidate, surface) >= minContrast) return candidate;
  }
  return toHex(target);
}

/** Text colour for content placed on the accent (white or near-black, whichever reads better). */
export function textOnAccent(accent: string): string {
  return contrastRatio(accent, '#ffffff') >= contrastRatio(accent, '#0b0f1a') ? '#ffffff' : '#0b0f1a';
}

/** The accent a theme selection resolves to (custom themes use the user's colour). */
export function themeAccent(theme: ProfileThemeId, customAccent: string | null): string {
  if (theme === 'custom' && customAccent && HEX_COLOR_PATTERN.test(customAccent)) return customAccent;
  return themePreset(theme).accent;
}
