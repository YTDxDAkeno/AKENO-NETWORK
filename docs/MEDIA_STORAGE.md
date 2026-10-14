# Media storage (uploaded images)

Profile pictures, profile banners and game artwork are stored by the AKENO server itself on a
persistent volume. Implementation: `apps/server/src/modules/media/`.

## Choice of backend

The VPS runs one app container next to PostgreSQL with 512 MB memory limits each (compose.yaml).
For the expected load (one avatar and one banner per user, a few hundred game images) the
simplest reliable option is a **dedicated Docker volume** (`media-data`, mounted at
`/data/media`). Object storage (S3-compatible) is not needed at this size; the code keeps all
filesystem access in `MediaService`, so another backend can replace it later.

Per-object size after processing (all four variants, measured 2026-10-14 with the production
encoder settings): smooth, photo-like images 17–64 KB; the incompressible worst case (random
noise) is avatar ≈ 200 KB, banner ≈ 715 KB, cover ≈ 500 KB, icon ≈ 260 KB. Real photos fall in
between. Even in the worst case 1,000 users with avatar and banner need ≈ 0.9 GB. The default global limit
is `MEDIA_MAX_TOTAL_MB=2048`; uploads are also refused when the volume has less than
`MEDIA_MIN_FREE_MB=512` free. Check the VPS disk before raising limits (`df -h`).

## Upload pipeline

1. **Authentication and scope.** Upload routes accept only a signed-in website session with the
   CSRF token (paired devices cannot upload). Unauthenticated requests are refused before the
   body is read; the raw-body parser exists only on the upload routes (8 MB limit); every other
   route still accepts only JSON (64 KB).
2. **Rate limits and quotas.** 12 uploads per 10 minutes per user (route limit), 30 profile
   uploads per user per 24 h (counted in the database, including replaced images), the global
   byte limit and the free-space check.
3. **Type detection from content.** The first bytes must be JPEG, PNG, WebP or GIF. File names
   and the declared MIME type are ignored. SVG, HTML, PDF, TIFF, HEIC and executables are
   refused before reaching the decoder; the decoder's detected format must agree.
4. **Bounded decoding.** Dimensions and pixel count are read from the header first (≤ 8192 px
   per side, ≤ 36 MP); libvips is limited the same way (`limitInputPixels`), fails on corrupt
   data, decodes only the first frame and has a 20 s timeout. One image is decoded at a time
   (`MEDIA_PROCESSING_CONCURRENCY=1`, queue of 20) so memory stays bounded.
5. **Normalization.** EXIF orientation is applied, the image is center-cropped to the kind's
   aspect ratio, converted to sRGB and scaled down (never up) into fixed variants: `lg.webp`,
   `md.webp`, `sm.webp` and `fallback.jpg`. Re-encoding drops **all** metadata (EXIF incl. GPS,
   XMP, ICC, comments). Animated GIF/WebP become still images.
6. **Storage.** A random 128-bit ID is generated; files are written to `MEDIA_DIR/tmp/<id>/`
   and moved to `MEDIA_DIR/objects/<id[0..2]>/<id>/` with a single rename, then the row is
   inserted in `media_objects`. If the insert fails, the files are removed.
7. **Replacement and deletion.** The old object is soft-deleted in the same transaction that
   attaches the new one and its files are removed right after the commit. Account deletion
   removes the user's images. Soft-deleted rows are purged after 48 h (they keep the daily
   quota honest). A maintenance sweep (every 15 min) also removes files without a live row that
   are older than an hour (for example after a crash between write and insert).

## Serving

`GET /media/<id>/<file>` requires a signed-in user or a paired device (`profile:read`). Paths
are built only from a validated 32-hex ID and one of four fixed file names, so no request can
reach other files. The `Content-Type` comes from the file name list (never from the upload),
with `X-Content-Type-Options: nosniff`, the app's CSP, `Cross-Origin-Resource-Policy:
same-origin`, an `ETag` and `Cache-Control: private, max-age=31536000, immutable` (a replaced
image gets a new ID, so caching forever is safe; `private` keeps shared caches out).

Privacy: media IDs are capabilities. They are only disclosed in API responses that already apply
profile visibility and blocks (for example the banner is `null` for viewers who may not see
profile details, and users who blocked you are not found at all). Someone who once received an
ID can keep loading that image while it exists; replacing or removing it ends that.

## Failure isolation

If `MEDIA_DIR` is not writable at startup, uploads answer `503 MEDIA_UNAVAILABLE` and everything
else (sign-in, messaging, presence, game activity) keeps working; tested in
`apps/server/test/media-profile.test.ts`. A missing or broken image falls back to the preset
avatar or the game's initials in the clients.

## Backup and restore

`infrastructure/scripts/backup.sh` writes the database dump **and** a media archive
(`akeno-media-<stamp>.tar.gz`); `restore.sh <dump> <media archive>` restores both. Verified on
2026-10-14 with the production Docker image: upload → backup → container restart (image still
served) → both volumes deleted → fresh stack → restore → the account's `avatarImage` and the
files were back and served. See VPS_DEPLOYMENT.md for the commands.

## Configuration

| Variable | Default | Meaning |
| --- | --- | --- |
| `MEDIA_DIR` | `./data/media` (Docker: `/data/media`) | Persistent directory |
| `MEDIA_MAX_TOTAL_MB` | 2048 | Upper bound for all stored media |
| `MEDIA_MIN_FREE_MB` | 512 | Refuse uploads below this free space |
| `MEDIA_PROCESSING_CONCURRENCY` | 1 | Images decoded at the same time |
