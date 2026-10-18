/*
 * The AKENO page of the OnionHEN Toolbox (EXPERIMENTAL): the companion model as an OnionHEN UI
 * document (OnionHEN plugin SDK, onion.ui ABI 1). The page is replaced as a whole, atomically, when
 * the data changes (same plugin and contribution ID).
 *
 *   AKENO NETWORK                         (page)
 *     Friends: 5 online                   (labels, the summary)
 *     Messages: 2 unread in 1 conversation
 *     ...
 *     > Friends online                    (menu -> page of labels)
 *     > Unread messages                   (menu -> page of labels)
 *     Status  [Online|Away|Busy|Invisible] (list, event "presence")
 *     Accept the party invitation         (action, confirmed, event "accept_invite")
 *     Refresh                             (action, event "refresh")
 */
#ifndef AKN_COMPANION_UI_DOC_H
#define AKN_COMPANION_UI_DOC_H

#include <onion/ui.h>

#include "companion.h"

#define AKN_COMPANION_PLUGIN_ID "AKEN00001"
#define AKN_COMPANION_VERSION "1.00"
#define AKN_COMPANION_CONTRIBUTION "akeno"

/* Event binding keys the plugin reacts to. */
#define AKN_EV_PRESENCE "presence"
#define AKN_EV_ACCEPT "accept_invite"
#define AKN_EV_REFRESH "refresh"

/* Builds and validates the document; the caller destroys it. */
onion_status akn_companion_document(const akn_companion *c, const char *note, onion_ui_document **out);

#endif
