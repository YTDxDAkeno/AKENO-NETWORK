/* The console UI (app/web) embedded into the executable at build time. */
#include "embed.h"

#ifndef AKENO_UI_DIR
#error "AKENO_UI_DIR must name the directory with index.html, app.js and app.css"
#endif

AKENO_EMBED_FILE(akeno_ui_index, AKENO_UI_DIR "/index.html");
AKENO_EMBED_FILE(akeno_ui_js, AKENO_UI_DIR "/app.js");
AKENO_EMBED_FILE(akeno_ui_css, AKENO_UI_DIR "/app.css");
