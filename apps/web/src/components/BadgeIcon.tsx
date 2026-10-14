import { BadgeCheck, Braces, Crown, FlaskConical, Gamepad2, ShieldCheck, Sprout, Ticket, type LucideIcon } from 'lucide-react';
import { createElement, useId } from 'react';
import { BADGES, badgeDefinition, type BadgeId } from '@akeno/shared';
import { cn } from './cn';

const ICONS: Record<BadgeId, LucideIcon> = {
  founder: Crown,
  administrator: ShieldCheck,
  developer: Braces,
  verified_contributor: BadgeCheck,
  early_supporter: Sprout,
  beta_tester: FlaskConical,
  ps5_alpha_tester: Gamepad2,
  event_participant: Ticket,
};

const SIZES = { sm: { box: 'size-5', icon: 'size-2.5' }, md: { box: 'size-7', icon: 'size-3.5' }, lg: { box: 'size-10', icon: 'size-5' } };

/**
 * Original AKENO badge artwork: a faceted hexagon medallion in the badge colour with a line icon.
 * Every badge shares the same shape and construction, so they read as one family.
 */
export function BadgeIcon({ id, size = 'md', className }: { id: BadgeId; size?: keyof typeof SIZES; className?: string }) {
  const badge = badgeDefinition(id) ?? BADGES[0]!;
  const gradientId = useId();
  const dims = SIZES[size];
  return (
    <span className={cn('relative inline-grid shrink-0 place-items-center', dims.box, className)} title={`${badge.name} — ${badge.description}`}>
      <svg viewBox="0 0 32 32" className="absolute inset-0 size-full" aria-hidden>
        <defs>
          <linearGradient id={gradientId} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor={badge.color} stopOpacity="0.95" />
            <stop offset="1" stopColor={badge.color} stopOpacity="0.62" />
          </linearGradient>
        </defs>
        <path d="M16 1.8 28.3 8.9v14.2L16 30.2 3.7 23.1V8.9Z" fill={`url(#${gradientId})`} />
        <path d="M16 1.8 28.3 8.9 16 16Z" fill="#fff" opacity="0.14" />
        <path d="M16 1.8 28.3 8.9v14.2L16 30.2 3.7 23.1V8.9Z" fill="none" stroke="#fff" strokeOpacity="0.35" strokeWidth="1.2" />
      </svg>
      {createElement(ICONS[badge.id], { 'aria-hidden': true, className: cn('relative text-[#0b0f1a]', dims.icon), strokeWidth: 2.4 })}
      <span className="sr-only">{badge.name}</span>
    </span>
  );
}

/** Medallion plus name, for profile headers and settings. */
export function BadgeChip({ id, className }: { id: BadgeId; className?: string }) {
  const badge = badgeDefinition(id);
  if (!badge) return null;
  return (
    <span
      className={cn('inline-flex items-center gap-1.5 rounded-full border border-line bg-surface-2/80 py-0.5 pl-0.5 pr-2.5 text-xs font-medium text-ink', className)}
      title={badge.description}
    >
      <BadgeIcon id={id} size="sm" />
      {badge.name}
    </span>
  );
}
