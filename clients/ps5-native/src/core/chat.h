/*
 * Messages in AKENO Native (Phase 2.2): the conversation list (direct conversations, your groups'
 * chats and your party's chat), one open thread, and the messages that arrive live through the
 * daemon's event ring. Everything comes from the existing server API through the daemon
 * (GET /conversations, GET /groups, GET /parties/current, GET /conversations/:id/messages) and is
 * written back through the daemon's native write list (POST /conversations/:id/messages and
 * /read). A message is sent with a client-generated UUID (clientMessageId): a retry after a
 * timeout or a reconnect can never create a second copy on the server, and the thread shows it
 * once, pending until the server confirms it. Portable and unit-tested.
 */
#ifndef AKN_CHAT_H
#define AKN_CHAT_H

#include <stddef.h>
#include <stdint.h>

#include "social.h"

struct akeno_json;

#define AKN_CHAT_CONVS 48
#define AKN_CHAT_MSGS 120
#define AKN_MSG_BODY 2400      /* bytes kept per message for display (the server allows 2000 characters) */
#define AKN_MSG_SEND_MAX 500   /* characters AKENO Native lets you type into one message */

typedef enum { AKN_CONV_DIRECT = 0, AKN_CONV_GROUP, AKN_CONV_PARTY } akn_conv_kind;

typedef enum { AKN_MSG_SENT = 0, AKN_MSG_SENDING, AKN_MSG_FAILED } akn_msg_state;

typedef struct {
    long long id;            /* the server's message ID; 0 while it is pending */
    char client_id[40];      /* clientMessageId (UUID) */
    char sender_id[40];
    char sender_name[80];
    char sender_avatar[33];
    char body[AKN_MSG_BODY];
    int truncated;           /* the body was longer than AKN_MSG_BODY bytes */
    long long created_at;    /* unix seconds */
    int mine;
    akn_msg_state state;
} akn_message;

typedef struct {
    akn_conv_kind kind;
    char id[40];             /* conversation ID */
    char target_id[40];      /* direct: the other user; group: the group; party: the party */
    char title[80];          /* the other user's name, the group's or the party's */
    char handle[40];         /* direct: the other user's Online ID */
    char avatar[33];         /* direct: the other user's picture (media ID) */
    akn_presence_view presence; /* direct */
    char preview[140];       /* the last message on one line */
    int preview_mine;
    long long last_at;       /* unix seconds, 0 when nothing was written yet */
    long long last_message_id;
    int unread;
    int can_send;
    int members;             /* groups and parties */
} akn_conversation;

typedef struct {
    akn_conversation items[AKN_CHAT_CONVS];
    int count;
    int truncated;
    int unread_total;        /* unread messages over all conversations */
} akn_conv_list;

typedef struct {
    char conversation_id[40];
    akn_message items[AKN_CHAT_MSGS]; /* confirmed messages by ID, oldest first, then pending ones */
    int count;
    int has_more;            /* older messages exist on the server */
    long long newest_id;     /* the newest confirmed message */
} akn_thread;

/* One DirectConversationDto (also the answer of POST /conversations). 0, or -1. */
int akn_parse_direct_conversation(const struct akeno_json *node, akn_conversation *out);
/* GET /conversations (direct conversations): replaces the list. 0, or -1 on an unexpected shape. */
int akn_fill_direct_conversations(const struct akeno_json *data, akn_conv_list *out);
/* GET /groups: adds a conversation for every group you are a member of. */
int akn_add_group_conversations(const struct akeno_json *data, akn_conv_list *out);
/* Adds (or updates) your party's chat. */
void akn_add_party_conversation(akn_conv_list *out, const char *conversation_id, const char *party_id, const char *name, int members,
                                int unread);
/* The party first, then the most recent conversation first, then by title; recounts unread_total. */
void akn_conversations_sort(akn_conv_list *list);
int akn_conversations_find(const akn_conv_list *list, const char *conversation_id);

/* One MessageDto. self_id: your user ID (decides "mine"). 0, or -1 when it is not a message. */
int akn_parse_message(const struct akeno_json *node, const char *self_id, akn_message *out);
/* GET .../messages (a MessagePage, oldest first): the thread's confirmed messages become this page
 * (older ones are dropped); pending messages stay at the end unless the page confirms them. */
int akn_fill_thread(const struct akeno_json *data, const char *self_id, akn_thread *t);
/* GET .../messages?before=<oldest>: adds an older page in front. */
int akn_thread_prepend(const struct akeno_json *data, const char *self_id, akn_thread *t);
/* Adds one confirmed message (an event or a send answer): ignored when the ID is there already; it
 * replaces the pending message with the same clientMessageId. Returns 1 when the thread changed. */
int akn_thread_add(akn_thread *t, const akn_message *m);
/* A message you are sending: shown at once as pending. Returns 0, or -1 when the thread is full
 * of pending messages. */
int akn_thread_add_pending(akn_thread *t, const char *client_id, const char *self_id, const char *self_name, const char *body,
                           long long now);
/* Marks the pending message failed (it stays on screen with "Not sent"), or sending again. */
void akn_thread_set_state(akn_thread *t, const char *client_id, akn_msg_state state);
int akn_thread_find_client(const akn_thread *t, const char *client_id);
/* The oldest confirmed message's ID (for ?before=), 0 when there is none. */
long long akn_thread_oldest_id(const akn_thread *t);

typedef struct {
    int messages;      /* message.created events seen */
    int in_open;       /* ... added to the open thread */
    int from_others;   /* ... written by someone else */
    int reload_list;   /* a conversation that is not in the list, or an event too large for the ring */
    int reload_thread; /* the open thread missed a message (it was too large for the ring) */
} akn_chat_events;

/* Applies message.created and conversation.read from GET /api/events to the list and the open
 * thread (either may be NULL). The open conversation's own messages do not count as unread. */
int akn_chat_apply_events(const char *json, size_t len, const char *self_id, akn_conv_list *list, akn_thread *open, akn_chat_events *out);

/* A version 4 UUID from 16 random bytes ("xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx", lower case). */
void akn_uuid_v4(const uint8_t random16[16], char out[37]);
/* The JSON body {"body":"...","clientMessageId":"..."} of a send. -1 when it does not fit. */
int akn_message_body_json(const char *body, const char *client_id, char *out, size_t len);
/* Escapes text as a JSON string (with the quotes). -1 when it does not fit or is not UTF-8. */
int akn_json_quote(const char *text, char *out, size_t len);
/* One line of a message for the list: line breaks become spaces, cut at a character boundary. */
void akn_chat_preview(const char *body, char *out, size_t len);
/* Validates a message before it is sent: non-empty after trimming, at most AKN_MSG_SEND_MAX
 * characters, valid UTF-8 without control characters other than line breaks. */
int akn_message_valid(const char *body);
/* Number of UTF-8 characters (code points). */
int akn_utf8_count(const char *s);

#endif
