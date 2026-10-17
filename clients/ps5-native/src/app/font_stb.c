/*
 * stb_truetype v1.26 (nothings/stb 6e9f34d, MIT or public domain), fetched with its checksum by
 * scripts/fetch-deps.sh. Compiled here once, with SDL's C runtime so the PS5 build needs nothing
 * beyond SDL and the SDK libc. Third-party code: built without AKENO's warning flags.
 */
#include <SDL.h>

#define STBTT_ifloor(x) ((int)SDL_floor(x))
#define STBTT_iceil(x) ((int)SDL_ceil(x))
#define STBTT_sqrt(x) SDL_sqrt(x)
#define STBTT_pow(x, y) SDL_pow(x, y)
#define STBTT_fmod(x, y) SDL_fmod(x, y)
#define STBTT_cos(x) SDL_cos(x)
#define STBTT_acos(x) SDL_acos(x)
#define STBTT_fabs(x) SDL_fabs(x)
#define STBTT_malloc(x, u) ((void)(u), SDL_malloc(x))
#define STBTT_free(x, u) ((void)(u), SDL_free(x))
#define STBTT_assert(x) SDL_assert(x)
#define STBTT_strlen(x) SDL_strlen(x)
#define STBTT_memcpy SDL_memcpy
#define STBTT_memset SDL_memset

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
