import { useEffect, useState, type CSSProperties } from 'react';
import { parseHexColor, readableAccent, textOnAccent, themeAccent, themePreset, type ProfileAppearance } from '@akeno/shared';
import { getTheme, type Theme } from './preferences';

const SURFACE: Record<Theme, string> = { dark: '#0d1323', light: '#ffffff' };

function rgba(hex: string, alpha: number): string {
  const rgb = parseHexColor(hex) ?? [79, 124, 255];
  return `rgb(${rgb[0]} ${rgb[1]} ${rgb[2]} / ${alpha})`;
}

function shade(hex: string, amount: number): string {
  const rgb = parseHexColor(hex);
  if (!rgb) return hex;
  return `#${rgb.map((channel) => Math.round(channel * (1 - amount)).toString(16).padStart(2, '0')).join('')}`;
}

/**
 * CSS custom properties for a profile theme. The accent is adjusted until it has at least 4.5:1
 * contrast with the surface (so accent-coloured text stays readable), and text on accent
 * backgrounds uses white or near-black, whichever reads better.
 */
export function themeTokens(appearance: Pick<ProfileAppearance, 'theme' | 'accentColor'>, mode: Theme): Record<string, string> {
  const raw = themeAccent(appearance.theme, appearance.accentColor);
  const accent = readableAccent(raw, SURFACE[mode], 4.5);
  const preset = themePreset(appearance.theme);
  return {
    '--akeno-accent': accent,
    '--akeno-accent-strong': shade(accent, mode === 'dark' ? 0.12 : 0.15),
    '--akeno-accent-soft': rgba(accent, mode === 'dark' ? 0.16 : 0.12),
    '--akeno-on-accent': textOnAccent(accent),
    '--akeno-profile-tint': mode === 'dark' ? preset.tint : '#ffffff',
  };
}

export function themeStyle(appearance: Pick<ProfileAppearance, 'theme' | 'accentColor'>, mode: Theme): CSSProperties {
  return themeTokens(appearance, mode) as CSSProperties;
}

/** Default banner of a theme (used when no banner was uploaded). */
export function defaultBannerStyle(appearance: Pick<ProfileAppearance, 'theme' | 'accentColor'>): CSSProperties {
  const preset = themePreset(appearance.theme);
  const accent = themeAccent(appearance.theme, appearance.accentColor);
  return {
    backgroundColor: preset.bannerTo,
    backgroundImage: `linear-gradient(135deg, ${preset.bannerFrom} 0%, ${preset.bannerTo} 70%), radial-gradient(120% 140% at 85% 0%, ${rgba(accent, 0.28)} 0%, transparent 55%)`,
    backgroundBlendMode: 'screen',
  };
}

/** The light/dark preference, kept in sync with the settings page. */
export function useColorMode(): Theme {
  const [mode, setMode] = useState<Theme>(getTheme);
  useEffect(() => {
    const onChange = () => setMode(getTheme());
    window.addEventListener('akeno:preferences', onChange);
    return () => window.removeEventListener('akeno:preferences', onChange);
  }, []);
  return mode;
}

/** Applies the signed-in user's theme accent to the whole app. */
export function useApplyUserTheme(appearance: Pick<ProfileAppearance, 'theme' | 'accentColor'> | null): void {
  const mode = useColorMode();
  const theme = appearance?.theme;
  const accentColor = appearance?.accentColor ?? null;
  useEffect(() => {
    const root = document.documentElement;
    const tokens = theme ? themeTokens({ theme, accentColor }, mode) : null;
    const names = ['--akeno-accent', '--akeno-accent-strong', '--akeno-accent-soft', '--akeno-on-accent', '--akeno-profile-tint'];
    for (const name of names) {
      if (tokens?.[name]) root.style.setProperty(name, tokens[name]);
      else root.style.removeProperty(name);
    }
    return () => {
      for (const name of names) root.style.removeProperty(name);
    };
  }, [theme, accentColor, mode]);
}
