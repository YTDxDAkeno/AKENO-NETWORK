# AKENO NETWORK — Alpha 0.3 (release candidate)

**Status: release candidate, not deployed.** All automated checks pass locally (see
PROJECT_STATUS.md, session 5). The production upgrade needs your approval and the steps in
"Upgrading" below. The PS5 console-page additions are tested on Linux only.

## New

- **Your own profile picture.** Upload from a PC or phone, crop, zoom and position it in the
  browser, preview it at several sizes and save. Replace or remove it at any time; the 16 preset
  avatars stay available. Pictures appear everywhere: profiles, friends, messages, parties,
  notifications, invitations, search, settings and the PS5 console page.
- **Profile banners** (3:1) with the same editor, an optional dark overlay, and a theme-coloured
  default banner.
- **Profile themes:** AKENO Default, Dark Purple, Cobalt Blue, Crimson, Midnight Black, Sakura
  Pink, Emerald, or a custom accent colour, with a live preview. Contrast is enforced
  automatically so text and buttons always stay readable. Your theme colours your profile for
  visitors and your own app.
- **Badges:** AKENO Founder, Administrator, Developer, Verified Contributor, Early Supporter,
  Beta Tester, PS5 Alpha Tester and Community Event Participant, with original artwork. Granted
  only by administrators (audited) or the server operator; feature up to three on your profile.
  These are AKENO recognitions, not PlayStation trophies.
- **Real game artwork.** Administrators maintain a game catalogue (Administration → Games):
  verified PlayStation title IDs, square icons, 2:3 covers and optional wide artwork, each with
  its source and licence recorded. Once a game is verified, its artwork appears automatically for
  everyone whenever a console reports it — friends list, "Now playing", recently played, history,
  profiles, favorites, game sessions and parties. A **detected titles** report shows which played
  games still need verification or artwork. Artwork is never matched by similar names.
- **On the PS5 console page:** profile pictures, server artwork and — for games installed on
  that console — the console's own game icons (read locally and never uploaded).
- **Redesigned profile page**: banner, large avatar, badges, Now playing with artwork, recently
  played as covers, favorite games, gaming activity totals, party card with a Join button, mutual
  friends. Redesigned party page with member tiles and current games, a "friends playing now"
  strip on Home, artwork on game sessions, a calmer colour system (solid accent colours instead
  of gradients).
- **Backups now include uploaded images** (`backup.sh` writes the database dump and a media
  archive; `restore.sh` restores both).

## Security and privacy

Uploads are validated by content (JPEG, PNG, WebP, GIF only), decoded with strict limits,
re-encoded with all camera metadata (including GPS location) removed, stored under random IDs on
a separate volume, rate-limited and quota-limited, and served only to signed-in users and paired
devices. Banners follow your profile visibility; badges cannot be self-assigned. Details:
MEDIA_STORAGE.md, PROFILES.md, GAME_ARTWORK.md, SECURITY.md.

## Upgrading

Migration `0004` is additive (new tables and profile columns; existing curated game names become
verified catalogue entries; `game_titles` stays for rollback). The upgrade was checked from a
real Alpha 0.2 database including rollback to Alpha 0.2. Before the production upgrade:

1. On the VPS, record the deployed commit (`git log -1`) and reconcile any VPS-only commits.
2. Back up (`./infrastructure/scripts/backup.sh`) and copy the backup off the server.
3. Run `node infrastructure/scripts/upgrade-check.mjs --from-dump <backup.dump> --old <checkout of the deployed commit>`
   on a disposable database.
4. Check free disk space for the new `media-data` volume (`df -h`).
5. Deploy (VPS_DEPLOYMENT.md → Upgrade). No firewall change is needed for Alpha 0.3.

## Known limitations

- No external artwork provider is integrated (reviewed in GAME_ARTWORK.md); artwork comes from
  administrators.
- Animated avatars are not supported (animated uploads become still pictures).
- Native PS5 notifications keep their predefined icons (no verified way to show custom images).
- Console-page images and local console icons are not yet tested on hardware.
