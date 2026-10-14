/**
 * AKENO community badges. These are recognitions inside this AKENO NETWORK server only; they are
 * not PlayStation trophies and are never presented as such.
 *
 * Badges are never self-assigned. "administrator" is derived from the account role; every other
 * badge is granted by an administrator (recorded in the audit log) or by the server operator
 * through the command line. Authority badges cannot be granted by an administrator to their own
 * account.
 */

export const BADGE_IDS = [
  'founder',
  'administrator',
  'developer',
  'verified_contributor',
  'early_supporter',
  'beta_tester',
  'ps5_alpha_tester',
  'event_participant',
] as const;
export type BadgeId = (typeof BADGE_IDS)[number];

export interface BadgeDefinition {
  id: BadgeId;
  name: string;
  description: string;
  /** Represents authority or special status (extra protections when granting). */
  authority: boolean;
  /** Derived by the server (never stored or granted manually). */
  derived: boolean;
  /** Medallion colour of the original AKENO badge artwork. */
  color: string;
}

export const BADGES: readonly BadgeDefinition[] = [
  { id: 'founder', name: 'AKENO Founder', description: 'Founded this AKENO NETWORK community.', authority: true, derived: false, color: '#e9b949' },
  { id: 'administrator', name: 'Administrator', description: 'Runs and moderates this AKENO NETWORK server.', authority: true, derived: true, color: '#ef5b5b' },
  { id: 'developer', name: 'Developer', description: 'Builds AKENO NETWORK.', authority: true, derived: false, color: '#38bdf8' },
  { id: 'verified_contributor', name: 'Verified Contributor', description: 'Contribution confirmed by the administrators.', authority: true, derived: false, color: '#34d399' },
  { id: 'early_supporter', name: 'Early Supporter', description: 'Supported this community from the start.', authority: false, derived: false, color: '#f472b6' },
  { id: 'beta_tester', name: 'Beta Tester', description: 'Tested AKENO NETWORK before release.', authority: false, derived: false, color: '#a78bfa' },
  { id: 'ps5_alpha_tester', name: 'PS5 Alpha Tester', description: 'Tested the AKENO console app on real PS5 hardware.', authority: false, derived: false, color: '#5b8cff' },
  { id: 'event_participant', name: 'Community Event Participant', description: 'Took part in an AKENO community event.', authority: false, derived: false, color: '#fb923c' },
];

/** Badges a user may feature prominently on their profile. */
export const FEATURED_BADGES_MAX = 3;

export function badgeDefinition(id: string): BadgeDefinition | undefined {
  return BADGES.find((badge) => badge.id === id);
}

/** Badges an administrator may grant (everything that is not derived). */
export const GRANTABLE_BADGE_IDS = BADGES.filter((badge) => !badge.derived).map((badge) => badge.id) as [BadgeId, ...BadgeId[]];
