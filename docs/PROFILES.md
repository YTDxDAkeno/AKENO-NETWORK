# Profiles: pictures, banners, themes and badges (Alpha 0.3)

## Profile pictures

- **Settings → Profile → Picture and banner → Upload** opens the crop dialog: choose a file (or
  drag it in; on phones the system picker offers the camera and the photo library), zoom with
  the slider, mouse wheel, pinch or `+`/`−`, move it by dragging or with the arrow keys, check
  the round previews at 96/48/32 px, then save.
- The browser only uploads the cropped square (≤ 1024 px, never enlarged). The server validates,
  re-encodes and strips metadata again (MEDIA_STORAGE.md) and stores 512/256/128 px WebP plus a
  256 px JPEG fallback. Pictures are shown as circles everywhere a user appears: profiles,
  friends, messages, parties, notifications, invitations, search, settings and the PS5 console
  page.
- **Replace** uploads a new picture (the old files are deleted). **Remove** goes back to the
  preset avatar. Choosing a **preset avatar** while a picture exists removes the picture.
- Existing users keep their preset avatars; `avatar` (preset ID) is always present in user
  summaries and `avatarImage` is `null` until a picture is uploaded. Older clients (the PS5
  console app before Alpha 0.3) simply ignore the new field.
- Native PS5 system notifications use predefined icons only; custom pictures are not sent to the
  notification system (no verified way to show arbitrary images there).
- Animated avatars are not supported yet (an animated upload becomes a still picture).

## Banners

- 3:1, uploaded through the same crop dialog; stored at 1500/960/600 px plus a 960 px JPEG.
- Without an upload, the theme's default banner (a quiet gradient with a line texture) is shown.
- **Darken banner** (default on) adds a soft dark overlay. Names, badges and buttons are never
  placed on the banner itself, so any image keeps them readable.
- Viewers who may not see profile details (friends-only profiles) do not receive the banner.

## Themes

Presets: AKENO Default, Dark Purple, Cobalt Blue (PlayStation-inspired), Crimson, Midnight Black,
Sakura Pink, Emerald, and a **custom accent colour**. A theme sets a small set of CSS tokens —
accent, accent-strong, accent-soft, text-on-accent and a profile tint — on:

- the owner's profile page, for everyone who visits it, and
- the owner's own app (the whole interface uses the token system: `bg-accent`, `text-accent`,
  `text-on-accent`, `card-tinted`, ...).

Readability is enforced in code (`packages/shared/src/themes.ts`, `apps/web/src/lib/theme.ts`):
the accent is lightened or darkened in small steps until it reaches 4.5:1 contrast with the
surface in the current light/dark mode, and text on accent backgrounds uses white or near-black,
whichever has the higher contrast. The settings page shows a live preview; **Reset to default**
restores AKENO Default with the overlay on. Tested in desktop and phone layouts (Chromium). The
PS5 console page keeps its own fixed palette; opening the full website in the PS5 system browser
has not been tested.

## Badges

AKENO badges are community recognitions on this server — **not** PlayStation trophies.

| Badge | How it is earned |
| --- | --- |
| Administrator | Derived from the account role (never stored, cannot be granted) |
| AKENO Founder, Developer, Verified Contributor | Authority badges: granted by an administrator *other than the recipient*, or by the server operator (`pnpm cli grant-badge --online-id <id> --badge founder`) |
| Early Supporter, Beta Tester, PS5 Alpha Tester, Community Event Participant | Granted by an administrator |

- **Administration → Members → Badges** grants and revokes badges with an optional note; every
  change is audited (`admin.badge_granted`, `admin.badge_revoked`, CLI grants with `viaCli`).
- There is no endpoint through which users can give themselves a badge. Users choose up to three
  **featured** badges among the ones they have earned (Settings → Profile → Appearance); the
  server rejects anything else, and revoked badges drop out of the selection.
- Badge artwork is original: one hexagonal medallion construction with a line icon and a colour
  per badge (`apps/web/src/components/BadgeIcon.tsx`).

## Redesigned profile page

Header: banner, large avatar with presence, display name, Online ID, featured badges (by name)
and other badges (medallions), status message, presence/joined/mutual friends, and actions
(Add friend / Accept / Message / Invite to party / more). Main column: **Now playing** (game
artwork, platform, duration; wide artwork as background when available), **Recently played**
(2:3 covers with last played, last session, total tracked playtime and session count) and
**Favorite games** (covers for exact matches to verified games). Side column: **Gaming activity**
(tracked playtime, games, sessions, tracked since — all from confirmed heartbeats, hidden when
the owner hides playtime), **Party** (join button when the viewer may join), **About** and
**Mutual friends**.

Privacy is unchanged: every value comes from the same server-side rules as before (invisible
mode, game activity/history audiences, excluded games, blocks, friends-only profiles). The party
card only appears while the user appears online to the viewer.
