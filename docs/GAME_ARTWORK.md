# Game artwork

Alpha 0.3 shows real game icons, box art and wide artwork wherever AKENO shows a game: friends
list activity lines, "Now playing" cards, recently played, game history, profiles, favorite
games, game sessions, party members and the PS5 console page.

## How a detected game gets its artwork

The paired console reports a PlayStation **title ID** (`PPSA01234` for PS5 games, `CUSA01234`
for PS4 games). The server owns all metadata resolution; clients only receive a stable
`GameArtwork` reference (`{ gameId, icon, cover, banner }` media IDs) and never call an artwork
provider themselves.

Resolution order (`apps/server/src/modules/games/service.ts`):

1. **Verified canonical mapping.** The title ID is in `game_title_mappings` and points to a game
   in `games`. That game's name and its administrator-approved artwork are used.
2. **Trusted provider result mapped by exact identifier.** Reserved for a licensed provider that
   maps PlayStation title IDs exactly (see "Providers reviewed" — none is integrated today).
3. **Administrator-supplied artwork** — in practice the same as 1: artwork is uploaded by an
   administrator onto a verified game record, with its source and licence recorded.
4. **Clean fallback.** No artwork: clients show a neutral tile with the game's initials (never a
   random illustration and never another game's image).

Rules that keep artwork correct:

- A title ID reaches a game **only** through a mapping an administrator created (or a curated
  Alpha 0.2 name, which migration `0004` turned into a verified mapping). The name a console
  reports is never used to attach artwork: a console reporting "Elden Ring" under an unmapped
  title ID shows the name, not the artwork (tested in `apps/server/test/games-badges.test.ts`).
- Favorite games and game sessions are free text. They get artwork only when the text equals a
  verified game's name or alias after normalization (case, Unicode form, ™/®/© and punctuation),
  or — for sessions — when the identifier field is a mapped title ID. "Elden Ring Nightreign"
  does not match "Elden Ring". A name shared by two games matches neither.
- Unknown title IDs stay unknown until an administrator verifies them.

## Image shapes

| Slot | Aspect | Variants (WebP + JPEG fallback) | Used for |
| --- | --- | --- | --- |
| `icon` | 1:1 | 512, 256, 128 px | lists, activity lines, compact cards |
| `cover` | 2:3 | 600×900, 300×450, 150×225 | recently played, favorites, libraries |
| `banner` | 16:9 | 1280×720, 640×360, 320×180 | large "Now playing" cards (optional) |

Uploads are center-cropped to the slot's aspect ratio and **never enlarged** (a 400×600 source
gives a 400×600 largest variant). A square icon is never stretched into a cover: when a game has
no cover, the cover frame shows the icon centred at its own aspect ratio on a blurred copy; an
icon frame may fall back to a center crop of the cover. Images are lazy-loaded with reserved
space (no layout shift) and served as immutable, cacheable files (see MEDIA_STORAGE.md).

## Administration

**Administration → Games**:

- **Detected titles** — every title ID consoles have reported, with the console-reported name,
  players, sessions, last played, and whether it is mapped and has artwork. "Create game" turns a
  title into a verified game (the reported name is only a suggestion; check it), "Add artwork"
  opens the game.
- **Catalogue** — search by name or title ID, filter "missing artwork only".
- **Game editor** — name, alternative names, title IDs (map/unmap), icon/cover/banner upload and
  removal, and **artwork source and rights**: source (`publisher press kit`, `licensed provider`,
  `own capture`, ...), attribution and licence/usage notes.
- **Reload** rebuilds the server's in-memory catalogue cache (it is also rebuilt after every
  change). Everyone currently playing an affected title gets a fresh presence update.

Every change is audited (`admin.game_created`, `admin.game_title_mapped`,
`admin.game_artwork_set`, ...). The Alpha 0.2 API `PUT/DELETE /admin/game-titles/:titleId` still
works: it creates or renames the game mapped to that title ID and keeps `game_titles` in step so
a rollback to Alpha 0.2 shows the same names.

## The PS5 console page

The console page shows, for each game, in this order: the server's artwork (if the title is
verified there), otherwise **the icon the console itself keeps for an installed title**
(`icon0.png` in `/user/appmeta/<title ID>/` or `/system_data/priv/appmeta/<title ID>/`, read
only, PNG signature checked, ≤ 1 MiB), otherwise the initials. Local icons are shown only on that
console's own page and are **never uploaded** to the AKENO server. Whether the payload can read
these files on firmware 12.20/13.60 is not yet verified on hardware (the same folders hold the
`param.json` files whose titles were read successfully on 12.20).

## Providers reviewed (not integrated)

AKENO does not download artwork from any external service in Alpha 0.3. Reviewed options:

| Source | Finding | Decision |
| --- | --- | --- |
| PlayStation Network / Store endpoints (incl. the title metadata servers used by consoles) | Protected or undocumented Sony services; using them would mean scraping or replicating console authentication | **Excluded** by policy |
| IGDB (Twitch) | Free for non-commercial use; commercial use requires a partnership; user-facing attribution expected; governed by the Twitch Developer Services Agreement; image rights for redistribution and permanent caching are not clearly granted; exact PlayStation title-ID mapping could not be verified without credentials | Not integrated. Could be added later as an *administrator-triggered candidate lookup* (never automatic assignment by name) once an operator has credentials and accepts the terms |
| SteamGridDB | Community uploads of mostly fan-made or derived art; rights unclear; keyed to PC/Steam IDs | Not suitable |
| RAWG / TheGamesDB | Attribution and request limits; no reliable PlayStation title-ID mapping | Not suitable for automatic mapping |
| Console-local metadata (`icon0.png`) | Legitimately present on the user's own console | Used read-only on the console page only; never uploaded |
| Administrator uploads (publisher press kits, own captures) | Rights are the operator's responsibility; source and licence are recorded per game | **The supported source** |

Anyone adding artwork should only use images they may show on their server and record where
they came from in the game editor.
