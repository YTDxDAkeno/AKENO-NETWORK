#include "ui_doc.h"

#include <stdio.h>
#include <string.h>

static onion_ui_node_desc_v1 node(uint32_t kind, const char *id, const char *parent, const char *title) {
    onion_ui_node_desc_v1 n;
    memset(&n, 0, sizeof(n));
    n.struct_size = sizeof(n);
    n.abi_version = ONION_UI_ABI_VERSION;
    n.kind = kind;
    snprintf(n.id, sizeof(n.id), "%s", id);
    snprintf(n.parent_id, sizeof(n.parent_id), "%s", parent ? parent : "");
    snprintf(n.title, sizeof(n.title), "%s", title);
    return n;
}

/* Text for the Toolbox: within its size, cut at a UTF-8 character boundary. */
static void fit(char *dst, size_t len, const char *src) {
    size_t n = strlen(src);
    if (n >= len) {
        n = len - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static onion_status label(onion_ui_document *d, const char *id, const char *parent, const char *title, const char *description) {
    onion_ui_node_desc_v1 n = node(ONION_UI_NODE_LABEL, id, parent, "");
    fit(n.title, sizeof(n.title), title);
    fit(n.description, sizeof(n.description), description ? description : "");
    return onion_ui_document_add_node(d, &n);
}

#define TRY(x)                        \
    do {                              \
        onion_status s_ = (x);        \
        if (s_ != ONION_OK) return s_; \
    } while (0)

static onion_status add_nodes(onion_ui_document *d, const akn_companion *c, const char *note) {
    onion_ui_node_desc_v1 n = node(ONION_UI_NODE_PAGE, "main", NULL, "AKENO NETWORK");
    TRY(onion_ui_document_add_node(d, &n));
    n = node(ONION_UI_NODE_PAGE, "online", NULL, "Friends online");
    TRY(onion_ui_document_add_node(d, &n));
    n = node(ONION_UI_NODE_PAGE, "unread", NULL, "Unread messages");
    TRY(onion_ui_document_add_node(d, &n));

    /* the summary */
    n = node(ONION_UI_NODE_GROUP, "summary", "main", "AKENO");
    TRY(onion_ui_document_add_node(d, &n));
    akn_companion_row rows[12];
    int count = akn_companion_summary(c, rows, 12);
    for (int i = 0; i < count; i++) {
        char id[24], title[ONION_UI_TITLE_MAX];
        snprintf(id, sizeof(id), "sum_%d", i);
        snprintf(title, sizeof(title), "%.40s: %.80s", rows[i].label, rows[i].value);
        const char *desc = strcmp(rows[i].label, "Party") == 0 && c->in_party ? c->party_voice : "";
        TRY(label(d, id, "summary", title, desc));
    }

    if (c->state == AKN_COMP_READY) {
        /* details on their own pages */
        n = node(ONION_UI_NODE_MENU, "open_online", "main", "Friends online");
        snprintf(n.target_id, sizeof(n.target_id), "online");
        snprintf(n.description, sizeof(n.description), "%d online", c->friends_online);
        TRY(onion_ui_document_add_node(d, &n));
        n = node(ONION_UI_NODE_MENU, "open_unread", "main", "Unread messages");
        snprintf(n.target_id, sizeof(n.target_id), "unread");
        snprintf(n.description, sizeof(n.description), "%d unread", c->unread_messages);
        TRY(onion_ui_document_add_node(d, &n));
        for (int i = 0; i < c->online_shown; i++) {
            char id[24];
            snprintf(id, sizeof(id), "friend_%d", i);
            TRY(label(d, id, "online", c->online[i].label, c->online[i].value));
        }
        if (!c->online_shown) TRY(label(d, "friend_none", "online", "None of your friends is online", ""));
        for (int i = 0; i < c->convs_shown; i++) {
            char id[24];
            snprintf(id, sizeof(id), "conv_%d", i);
            TRY(label(d, id, "unread", c->convs[i].label, c->convs[i].value));
        }
        if (!c->convs_shown) TRY(label(d, "conv_none", "unread", "No unread messages", ""));
        TRY(label(d, "unread_hint", "unread", "Reading and answering", "Open AKENO NETWORK Native when you are not playing."));

        /* actions, only when the daemon takes changes */
        n = node(ONION_UI_NODE_GROUP, "actions", "main", "Quick actions");
        TRY(onion_ui_document_add_node(d, &n));
        n = node(ONION_UI_NODE_LIST, "presence", "actions", "Status");
        n.value_type = ONION_UI_VALUE_STRING;
        n.binding = ONION_UI_BINDING_EVENT;
        snprintf(n.binding_key, sizeof(n.binding_key), AKN_EV_PRESENCE);
        snprintf(n.value, sizeof(n.value), "%s", c->presence[0] ? c->presence : "online");
        if (!c->writes) n.flags |= ONION_UI_NODE_FLAG_DISABLED;
        TRY(onion_ui_document_add_node(d, &n));
        static const char *const values[] = { "online", "away", "dnd", "invisible" };
        static const char *const titles[] = { "Online", "Away", "Busy", "Invisible" };
        for (int i = 0; i < 4; i++) {
            char id[24];
            snprintf(id, sizeof(id), "presence_%s", values[i]);
            n = node(ONION_UI_NODE_LIST_ITEM, id, "presence", titles[i]);
            n.value_type = ONION_UI_VALUE_STRING;
            snprintf(n.value, sizeof(n.value), "%s", values[i]);
            TRY(onion_ui_document_add_node(d, &n));
        }
        n = node(ONION_UI_NODE_ACTION, "accept_invite", "actions", "Accept the party invitation");
        n.binding = ONION_UI_BINDING_EVENT;
        snprintf(n.binding_key, sizeof(n.binding_key), AKN_EV_ACCEPT);
        fit(n.description, sizeof(n.description), c->invite_line[0] ? c->invite_line : "No party invitation waiting");
        n.flags = ONION_UI_NODE_FLAG_CONFIRM | (c->invite_id[0] && c->writes ? 0 : ONION_UI_NODE_FLAG_DISABLED);
        TRY(onion_ui_document_add_node(d, &n));
    }
    n = node(ONION_UI_NODE_ACTION, "refresh", "main", "Refresh");
    n.binding = ONION_UI_BINDING_EVENT;
    snprintf(n.binding_key, sizeof(n.binding_key), AKN_EV_REFRESH);
    TRY(onion_ui_document_add_node(d, &n));
    TRY(label(d, "note", "main", "AKENO In-Game Companion (experimental)", note ? note : ""));
    return ONION_OK;
}

onion_status akn_companion_document(const akn_companion *c, const char *note, onion_ui_document **out) {
    *out = NULL;
    onion_ui_document_desc_v1 desc;
    memset(&desc, 0, sizeof(desc));
    desc.struct_size = sizeof(desc);
    desc.abi_version = ONION_UI_ABI_VERSION;
    desc.priority = 50;
    snprintf(desc.plugin_id, sizeof(desc.plugin_id), AKN_COMPANION_PLUGIN_ID);
    snprintf(desc.contribution_id, sizeof(desc.contribution_id), AKN_COMPANION_CONTRIBUTION);
    snprintf(desc.title, sizeof(desc.title), "AKENO NETWORK");
    snprintf(desc.description, sizeof(desc.description), "Friends, messages, party and invitations while you play");
    snprintf(desc.root_page_id, sizeof(desc.root_page_id), "main");
    onion_ui_document *d = NULL;
    onion_status st = onion_ui_document_create(&desc, &d);
    if (st == ONION_OK) st = add_nodes(d, c, note);
    if (st == ONION_OK) st = onion_ui_document_validate(d);
    if (st != ONION_OK) {
        onion_ui_document_destroy(d);
        return st;
    }
    *out = d;
    return ONION_OK;
}
