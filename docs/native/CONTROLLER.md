# Controller — AKENO NETWORK Native 1.0.0-beta.1

AKENO Native uses the DualSense directly (the same SDL controller path that Native 0.1 proved on a
PS5 with firmware 12.20). The bar at the bottom of every screen shows the buttons that work there.

## Everywhere

| Button | Action |
| --- | --- |
| D-pad / left stick | move the focus (hold to repeat) |
| ✕ | select / open |
| ◯ | back; on Home: the Exit dialog |
| △ | reload the list, profile or conversation |
| L1 / R1 | page up / down in lists, profiles and chats |

## Per screen

| Screen | ✕ | □ | other |
| --- | --- | --- | --- |
| Friends, Activity | open the profile | write to the friend | |
| Profile (a friend's) | — | write to them | up/down scroll |
| Messages | open the conversation | | |
| Chat | write (opens the keyboard) | the other person's profile | up/down scroll; at the top, up or L1 loads older messages |
| Party | create, open the party chat, invite, accept an invitation, leave | decline an invitation / write to a member | leaving and declining ask for confirmation |
| Inbox | accept the request or invitation; mark notifications read | decline (asks for confirmation) | |
| Settings | change the setting or open the check | previous status (Online status row) | |
| Invite a friend (picker) | invite / write | | |
| Controller test | every button is shown instead of acting | | hold ◯ for 1 s to return |
| Party, voice rows (1.1) | Join / Leave voice; mute; deafen | volume rows: lower | Left/Right on the volume rows: −/+ 10 % |
| Settings, Microphone test (1.1) | start / stop | hear yourself on / off | |
| Confirmation dialogs | the highlighted choice | | ◯ cancels |

## The on-screen keyboard

AKENO Native draws its own keyboard (no system dialog), so it needs no system function beyond what
Native 0.1 already used.

| Button | Action |
| --- | --- |
| D-pad / left stick | move between keys |
| ✕ | type the key |
| □ | delete the character before the cursor |
| △ | space |
| L1 | Shift (one letter); twice: Caps Lock |
| R1 | letters ↔ symbols (symbols include é è à ü ö ä ñ ç ß € £) |
| L2 / R2 | move the cursor left / right |
| OPTIONS | send (or "done" for names) |
| ◯ | close; the text stays as a draft for that conversation |

The bottom row has Shift, the symbols key, Space, Delete and Send for the same actions. Messages are
limited to 500 characters (the counter shows it). A USB keyboard types into the keyboard where the
system delivers text (Enter sends, Shift+Enter starts a new line).

## On a PC (tests and development)

Arrows: D-pad · Enter/Space: ✕ · Esc/Backspace: ◯ · Tab: OPTIONS · Q/E: L1/R1. In the keyboard,
typing works directly.
