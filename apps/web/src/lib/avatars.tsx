import { createElement } from 'react';
import {
  Bot,
  Cpu,
  Crosshair,
  Crown,
  Flame,
  Gamepad2,
  Ghost,
  type LucideIcon,
  Moon,
  Orbit,
  Rocket,
  Shield,
  Skull,
  Sparkles,
  Star,
  Sword,
  Zap,
} from 'lucide-react';
import { AVATAR_PRESETS, type AvatarPreset } from '@akeno/shared';

const ICONS: Record<AvatarPreset['icon'], LucideIcon> = {
  sparkles: Sparkles,
  zap: Zap,
  flame: Flame,
  ghost: Ghost,
  orbit: Orbit,
  crown: Crown,
  sword: Sword,
  rocket: Rocket,
  moon: Moon,
  cpu: Cpu,
  skull: Skull,
  crosshair: Crosshair,
  shield: Shield,
  star: Star,
  gamepad: Gamepad2,
  bot: Bot,
};

export function avatarPreset(id: string): AvatarPreset {
  return AVATAR_PRESETS.find((preset) => preset.id === id) ?? AVATAR_PRESETS[0];
}

export function avatarIcon(id: string): LucideIcon {
  return ICONS[avatarPreset(id).icon];
}

/** Renders the preset's icon. Icons come from a static map, never created per render. */
export function AvatarGlyph({ avatar, className }: { avatar: string; className?: string }) {
  return createElement(avatarIcon(avatar), { 'aria-hidden': true, className });
}
