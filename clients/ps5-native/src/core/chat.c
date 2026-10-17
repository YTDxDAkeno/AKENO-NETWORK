#include "chat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"
#include "akeno/json.h"
#include "image.h"
#include "text.h"

static void copy_str(char *dst, size_t len, const char *src) {
    if (!len) return;
    snprintf(dst, len, "%s", src ? src : "");
}

static const char *str_of(const akeno_json *object, const char *key) {
    const akeno_json *v = akeno_json_get(object, key);
    return v && v->type == AKENO_JSON_STRING ? v->string : "";
}

static long long int_of(const akeno_json *object, const char *key, long long fallback) {
    int64_t v;
    return akeno_json_int64(akeno_json_get(object, key), &v) == 0 ? (long long)v : fallback;
}

static long long time_of(const akeno_json *object, const char *key) {
    long long t;
    return akn_parse_iso8601(str_of(object, key), &t) == 0 ? t : 0;
}

static void copy_media(char out[33], const char *s) {
    out[0] = '\0';
    if (akn_is_media_id(s)) copy_str(out, 33, s);
}

/* Copies at most len-1 bytes of UTF-8 without splitting a character; returns 1 when cut. */
static int copy_utf8(char *dst, size_t len, const char *src) {
    size_t n = strlen(src);
    if (n < len) {
        memcpy(dst, src, n + 1);
        return 0;
    }
    size_t cut = len - 1;
    while (cut > 0 && ((unsigned char)src[cut] & 0xC0) == 0x80) cut--;
    memcpy(dst, src, cut);
    dst[cut] = '\0';
    return 1;
}

int akn_utf8_count(const char *s) {
    int n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

void akn_chat_preview(const char *body, char *out, size_t len) {
    if (!len) return;
    size_t o = 0;
    int space = 0;
    for (const char *p = body; *p && o + 1 < len; p++) {
        char c = *p;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ') {
            if (space || o == 0) continue;
            space = 1;
        } else {
            space = 0;
        }
        out[o++] = c;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    /* never end inside a UTF-8 sequence */
    size_t cut = o;
    while (cut > 0 && ((unsigned char)out[cut - 1] & 0xC0) == 0x80) cut--;
    if (cut > 0 && ((unsigned char)out[cut - 1] & 0x80)) {
        unsigned char lead = (unsigned char)out[cut - 1];
        size_t need = (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
        if (o - (cut - 1) < need) o = cut - 1;
    }
    out[o] = '\0';
}

int akn_message_valid(const char *body) {
    if (!body) return 0;
    size_t len = strlen(body);
    int visible = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        size_t n = akn_utf8_next(body + i, len - i, &cp);
        if (n == 0 || cp == 0xFFFD) return 0; /* invalid UTF-8 */
        if (cp < 0x20 && cp != '\n') return 0;
        if (cp == 0x7F) return 0;
        if (cp != ' ' && cp != '\n') visible = 1;
        i += n;
    }
    return visible && akn_utf8_count(body) <= AKN_MSG_SEND_MAX;
}

/* ---- Conversations ------------------------------------------------------------------------------ */

static void set_preview(akn_conversation *c, const akeno_json *last, const char *self_id) {
    c->preview[0] = '\0';
    c->preview_mine = 0;
    if (!last || last->type != AKENO_JSON_OBJECT) return;
    akn_chat_preview(str_of(last, "body"), c->preview, sizeof(c->preview));
    const akeno_json *sender = akeno_json_get(last, "sender");
    c->preview_mine = self_id && self_id[0] && strcmp(str_of(sender, "id"), self_id) == 0;
    long long id = int_of(last, "id", 0);
    if (id > c->last_message_id) c->last_message_id = id;
}

int akn_fill_direct_conversations(const akeno_json *data, akn_conv_list *out) {
    memset(out, 0, sizeof(*out));
    const akeno_json *arr = akeno_json_get(data, "conversations");
    if (!arr || arr->type != AKENO_JSON_ARRAY) return -1;
    for (const akeno_json *c = arr->child; c; c = c->next) {
        if (out->count >= AKN_CHAT_CONVS) {
            out->truncated = 1;
            break;
        }
        const akeno_json *other = akeno_json_get(c, "otherUser");
        const char *id = str_of(c, "id");
        if (!id[0] || !other || other->type != AKENO_JSON_OBJECT) continue;
        akn_conversation *v = &out->items[out->count];
        memset(v, 0, sizeof(*v));
        v->kind = AKN_CONV_DIRECT;
        copy_str(v->id, sizeof(v->id), id);
        copy_str(v->target_id, sizeof(v->target_id), str_of(other, "id"));
        copy_str(v->handle, sizeof(v->handle), str_of(other, "onlineId"));
        const char *dn = str_of(other, "displayName");
        copy_utf8(v->title, sizeof(v->title), dn[0] ? dn : v->handle);
        copy_media(v->avatar, str_of(other, "avatarImage"));
        akn_fill_presence(akeno_json_get(other, "presence"), &v->presence);
        v->last_at = time_of(c, "lastMessageAt");
        v->unread = (int)int_of(c, "unreadCount", 0);
        if (v->unread < 0) v->unread = 0;
        v->can_send = akeno_json_get_bool(c, "canSend", 0);
        v->members = 2;
        set_preview(v, akeno_json_get(c, "lastMessage"), NULL);
        /* "mine" needs your ID: the sender is the other user unless it is not */
        const akeno_json *last = akeno_json_get(c, "lastMessage");
        if (last && last->type == AKENO_JSON_OBJECT) v->preview_mine = strcmp(str_of(akeno_json_get(last, "sender"), "id"), v->target_id) != 0;
        out->count++;
    }
    akn_conversations_sort(out);
    return 0;
}

int akn_add_group_conversations(const akeno_json *data, akn_conv_list *out) {
    const akeno_json *arr = akeno_json_get(data, "groups");
    if (!arr || arr->type != AKENO_JSON_ARRAY) return -1;
    for (const akeno_json *g = arr->child; g; g = g->next) {
        const char *conv = str_of(g, "conversationId");
        const akeno_json *role = akeno_json_get(g, "myRole");
        if (!conv[0] || !role || role->type != AKENO_JSON_STRING) continue; /* not a member */
        int i = akn_conversations_find(out, conv);
        if (i < 0) {
            if (out->count >= AKN_CHAT_CONVS) {
                out->truncated = 1;
                break;
            }
            i = out->count++;
        }
        akn_conversation *v = &out->items[i];
        memset(v, 0, sizeof(*v));
        v->kind = AKN_CONV_GROUP;
        copy_str(v->id, sizeof(v->id), conv);
        copy_str(v->target_id, sizeof(v->target_id), str_of(g, "id"));
        copy_utf8(v->title, sizeof(v->title), str_of(g, "name"));
        if (!v->title[0]) copy_str(v->title, sizeof(v->title), "Group");
        v->unread = (int)int_of(g, "unreadCount", 0);
        if (v->unread < 0) v->unread = 0;
        v->members = (int)int_of(g, "memberCount", 0);
        v->can_send = 1;
        v->last_at = time_of(g, "updatedAt");
    }
    akn_conversations_sort(out);
    return 0;
}

void akn_add_party_conversation(akn_conv_list *out, const char *conversation_id, const char *party_id, const char *name, int members,
                                int unread) {
    if (!conversation_id || !conversation_id[0]) return;
    /* only one party at a time: an earlier party's chat leaves the list */
    for (int i = 0; i < out->count; i++)
        if (out->items[i].kind == AKN_CONV_PARTY && strcmp(out->items[i].id, conversation_id) != 0) {
            memmove(&out->items[i], &out->items[i + 1], (size_t)(out->count - i - 1) * sizeof(out->items[0]));
            out->count--;
            i--;
        }
    int i = akn_conversations_find(out, conversation_id);
    if (i < 0) {
        if (out->count >= AKN_CHAT_CONVS) out->count = AKN_CHAT_CONVS - 1; /* the party always gets a place */
        i = out->count++;
        memset(&out->items[i], 0, sizeof(out->items[i]));
        out->items[i].unread = unread;
    }
    akn_conversation *v = &out->items[i];
    v->kind = AKN_CONV_PARTY;
    copy_str(v->id, sizeof(v->id), conversation_id);
    copy_str(v->target_id, sizeof(v->target_id), party_id);
    copy_utf8(v->title, sizeof(v->title), name && name[0] ? name : "Party");
    v->members = members;
    v->can_send = 1;
    if (unread > v->unread) v->unread = unread;
    akn_conversations_sort(out);
}

static int conv_cmp(const void *pa, const void *pb) {
    const akn_conversation *a = pa, *b = pb;
    if ((a->kind == AKN_CONV_PARTY) != (b->kind == AKN_CONV_PARTY)) return a->kind == AKN_CONV_PARTY ? -1 : 1;
    if (a->last_at != b->last_at) return a->last_at > b->last_at ? -1 : 1;
    int c = strcmp(a->title, b->title);
    return c ? c : strcmp(a->id, b->id);
}

void akn_conversations_sort(akn_conv_list *list) {
    if (list->count > 1) qsort(list->items, (size_t)list->count, sizeof(list->items[0]), conv_cmp);
    list->unread_total = 0;
    for (int i = 0; i < list->count; i++) list->unread_total += list->items[i].unread;
}

int akn_conversations_find(const akn_conv_list *list, const char *conversation_id) {
    if (!list || !conversation_id || !conversation_id[0]) return -1;
    for (int i = 0; i < list->count; i++)
        if (strcmp(list->items[i].id, conversation_id) == 0) return i;
    return -1;
}

/* ---- Messages and threads ----------------------------------------------------------------------- */

int akn_parse_message(const akeno_json *node, const char *self_id, akn_message *out) {
    memset(out, 0, sizeof(*out));
    if (!node || node->type != AKENO_JSON_OBJECT) return -1;
    long long id = int_of(node, "id", 0);
    const akeno_json *sender = akeno_json_get(node, "sender");
    const akeno_json *body = akeno_json_get(node, "body");
    if (id <= 0 || !sender || sender->type != AKENO_JSON_OBJECT || !body || body->type != AKENO_JSON_STRING) return -1;
    out->id = id;
    copy_str(out->client_id, sizeof(out->client_id), str_of(node, "clientMessageId"));
    copy_str(out->sender_id, sizeof(out->sender_id), str_of(sender, "id"));
    const char *dn = str_of(sender, "displayName");
    copy_utf8(out->sender_name, sizeof(out->sender_name), dn[0] ? dn : str_of(sender, "onlineId"));
    copy_media(out->sender_avatar, str_of(sender, "avatarImage"));
    out->truncated = copy_utf8(out->body, sizeof(out->body), body->string);
    out->created_at = time_of(node, "createdAt");
    out->mine = self_id && self_id[0] && strcmp(out->sender_id, self_id) == 0;
    out->state = AKN_MSG_SENT;
    return 0;
}

static int confirmed_count(const akn_thread *t) {
    int n = 0;
    while (n < t->count && t->items[n].id > 0) n++;
    return n;
}

static void recompute_newest(akn_thread *t) {
    t->newest_id = 0;
    for (int i = 0; i < t->count; i++)
        if (t->items[i].id > t->newest_id) t->newest_id = t->items[i].id;
}

int akn_thread_find_client(const akn_thread *t, const char *client_id) {
    if (!client_id || !client_id[0]) return -1;
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->items[i].client_id, client_id) == 0) return i;
    return -1;
}

static void remove_at(akn_thread *t, int i) {
    memmove(&t->items[i], &t->items[i + 1], (size_t)(t->count - i - 1) * sizeof(t->items[0]));
    t->count--;
}

int akn_thread_add(akn_thread *t, const akn_message *m) {
    if (m->id <= 0) return 0;
    int confirmed = confirmed_count(t);
    for (int i = 0; i < confirmed; i++)
        if (t->items[i].id == m->id) return 0;
    /* the server confirmed a message we were sending: it leaves the pending tail */
    int p = akn_thread_find_client(t, m->client_id);
    if (p >= confirmed) remove_at(t, p);
    /* insert by ID among the confirmed messages */
    int pos = confirmed;
    while (pos > 0 && t->items[pos - 1].id > m->id) pos--;
    if (t->count >= AKN_CHAT_MSGS) {
        if (pos == 0) return p >= confirmed; /* older than everything kept */
        remove_at(t, 0);                     /* drop the oldest */
        pos--;
        t->has_more = 1;
    }
    memmove(&t->items[pos + 1], &t->items[pos], (size_t)(t->count - pos) * sizeof(t->items[0]));
    t->items[pos] = *m;
    t->count++;
    recompute_newest(t);
    return 1;
}

int akn_thread_add_pending(akn_thread *t, const char *client_id, const char *self_id, const char *self_name, const char *body, long long now) {
    if (akn_thread_find_client(t, client_id) >= 0) return 0; /* already there (a retry) */
    if (t->count >= AKN_CHAT_MSGS) {
        if (confirmed_count(t) == 0) return -1;
        remove_at(t, 0);
        t->has_more = 1;
    }
    akn_message *m = &t->items[t->count++];
    memset(m, 0, sizeof(*m));
    copy_str(m->client_id, sizeof(m->client_id), client_id);
    copy_str(m->sender_id, sizeof(m->sender_id), self_id);
    copy_utf8(m->sender_name, sizeof(m->sender_name), self_name);
    m->truncated = copy_utf8(m->body, sizeof(m->body), body);
    m->created_at = now;
    m->mine = 1;
    m->state = AKN_MSG_SENDING;
    return 0;
}

void akn_thread_set_state(akn_thread *t, const char *client_id, akn_msg_state state) {
    int i = akn_thread_find_client(t, client_id);
    if (i >= 0 && t->items[i].id == 0) t->items[i].state = state;
}

long long akn_thread_oldest_id(const akn_thread *t) {
    return t->count > 0 && t->items[0].id > 0 ? t->items[0].id : 0;
}

/* Parses a page into a heap array; returns the count or -1. */
static int parse_page(const akeno_json *data, const char *self_id, akn_message **out, int *has_more) {
    *out = NULL;
    const akeno_json *arr = akeno_json_get(data, "messages");
    if (!arr || arr->type != AKENO_JSON_ARRAY) return -1;
    *has_more = akeno_json_get_bool(data, "hasMore", 0);
    akn_message *msgs = calloc(arr->count ? arr->count : 1, sizeof(*msgs));
    if (!msgs) return -1;
    int n = 0;
    for (const akeno_json *m = arr->child; m; m = m->next)
        if (akn_parse_message(m, self_id, &msgs[n]) == 0) n++;
    *out = msgs;
    return n;
}

int akn_fill_thread(const akeno_json *data, const char *self_id, akn_thread *t) {
    akn_message *page;
    int has_more = 0;
    int n = parse_page(data, self_id, &page, &has_more);
    if (n < 0) return -1;
    /* keep the pending tail, drop the confirmed messages, then add the page */
    int confirmed = confirmed_count(t);
    memmove(&t->items[0], &t->items[confirmed], (size_t)(t->count - confirmed) * sizeof(t->items[0]));
    t->count -= confirmed;
    t->newest_id = 0;
    int start = n > AKN_CHAT_MSGS - t->count ? n - (AKN_CHAT_MSGS - t->count) : 0;
    for (int i = start; i < n; i++) akn_thread_add(t, &page[i]);
    t->has_more = has_more || start > 0;
    free(page);
    return 0;
}

int akn_thread_prepend(const akeno_json *data, const char *self_id, akn_thread *t) {
    akn_message *page;
    int has_more = 0;
    int n = parse_page(data, self_id, &page, &has_more);
    if (n < 0) return -1;
    int room = AKN_CHAT_MSGS - t->count;
    int take = n < room ? n : room;
    /* the newest of the older page, directly in front of what is there */
    for (int i = n - take; i < n; i++) akn_thread_add(t, &page[i]);
    t->has_more = has_more || take < n;
    free(page);
    return 0;
}

/* ---- Live events ---------------------------------------------------------------------------------- */

int akn_chat_apply_events(const char *json, size_t len, const char *self_id, akn_conv_list *list, akn_thread *open, akn_chat_events *out) {
    memset(out, 0, sizeof(*out));
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    const akeno_json *events = akeno_json_get(root, "events");
    if (!akeno_json_get_bool(root, "ok", 0) || !events || events->type != AKENO_JSON_ARRAY) {
        akeno_json_free(doc);
        return -1;
    }
    int changed_list = 0;
    for (const akeno_json *e = events->child; e; e = e->next) {
        const char *type = str_of(e, "type");
        const akeno_json *data = akeno_json_get(e, "data");
        if (strcmp(type, "message.created") == 0) {
            out->messages++;
            const char *conv = str_of(data, "conversationId");
            akn_message m;
            if (!conv[0] || akn_parse_message(akeno_json_get(data, "message"), self_id, &m) != 0) {
                /* too large for the daemon's ring (data null): reload what is shown */
                out->reload_list = 1;
                if (open && open->conversation_id[0]) out->reload_thread = 1;
                continue;
            }
            if (!m.mine) out->from_others++;
            int is_open = open && strcmp(open->conversation_id, conv) == 0;
            if (is_open && akn_thread_add(open, &m)) out->in_open++;
            int i = list ? akn_conversations_find(list, conv) : -1;
            if (i < 0) {
                if (list) out->reload_list = 1;
                continue;
            }
            akn_conversation *c = &list->items[i];
            if (m.id > c->last_message_id) {
                c->last_message_id = m.id;
                akn_chat_preview(m.body, c->preview, sizeof(c->preview));
                c->preview_mine = m.mine;
                if (m.created_at) c->last_at = m.created_at;
                if (!m.mine && !is_open) c->unread++;
                changed_list = 1;
            }
        } else if (strcmp(type, "conversation.read") == 0) {
            const char *conv = str_of(data, "conversationId");
            long long read = int_of(data, "lastReadMessageId", -1);
            int i = list ? akn_conversations_find(list, conv) : -1;
            if (i >= 0 && read >= 0 && read >= list->items[i].last_message_id && list->items[i].unread) {
                list->items[i].unread = 0;
                changed_list = 1;
            } else if (i >= 0 && read >= 0 && list->items[i].unread) {
                out->reload_list = 1; /* read up to an earlier message: the server knows the count */
            }
        }
    }
    if (changed_list) akn_conversations_sort(list);
    akeno_json_free(doc);
    return 0;
}

/* ---- Sending ---------------------------------------------------------------------------------------- */

void akn_uuid_v4(const uint8_t random16[16], char out[37]) {
    static const char hex[] = "0123456789abcdef";
    uint8_t b[16];
    memcpy(b, random16, 16);
    b[6] = (uint8_t)((b[6] & 0x0F) | 0x40);
    b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);
    size_t o = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
        out[o++] = hex[b[i] >> 4];
        out[o++] = hex[b[i] & 15];
    }
    out[o] = '\0';
}

int akn_json_quote(const char *text, char *out, size_t len) {
    static const char hex[] = "0123456789abcdef";
    size_t o = 0, n = strlen(text);
    if (len < 3) return -1;
    out[o++] = '"';
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t step = akn_utf8_next(text + i, n - i, &cp);
        if (step == 0 || (cp == 0xFFFD && !(step == 3 && memcmp(text + i, "\xEF\xBF\xBD", 3) == 0))) return -1;
        char esc[8];
        size_t el = 0;
        if (cp == '"') el = (size_t)snprintf(esc, sizeof(esc), "\\\"");
        else if (cp == '\\') el = (size_t)snprintf(esc, sizeof(esc), "\\\\");
        else if (cp == '\n') el = (size_t)snprintf(esc, sizeof(esc), "\\n");
        else if (cp == '\r') el = (size_t)snprintf(esc, sizeof(esc), "\\r");
        else if (cp == '\t') el = (size_t)snprintf(esc, sizeof(esc), "\\t");
        else if (cp < 0x20) {
            esc[0] = '\\', esc[1] = 'u', esc[2] = '0', esc[3] = '0', esc[4] = hex[cp >> 4], esc[5] = hex[cp & 15];
            el = 6;
        }
        if (el) {
            if (o + el + 2 > len) return -1;
            memcpy(out + o, esc, el);
            o += el;
        } else {
            if (o + step + 2 > len) return -1;
            memcpy(out + o, text + i, step);
            o += step;
        }
        i += step;
    }
    out[o++] = '"';
    out[o] = '\0';
    return 0;
}

int akn_message_body_json(const char *body, const char *client_id, char *out, size_t len) {
    if (!akn_message_valid(body) || strlen(client_id) != 36) return -1;
    size_t quoted_len = strlen(body) * 6 + 3;
    char *quoted = malloc(quoted_len);
    if (!quoted) return -1;
    int rc = akn_json_quote(body, quoted, quoted_len);
    if (rc == 0) {
        int n = snprintf(out, len, "{\"body\":%s,\"clientMessageId\":\"%s\"}", quoted, client_id);
        rc = n > 0 && (size_t)n < len ? 0 : -1;
    }
    free(quoted);
    return rc;
}
