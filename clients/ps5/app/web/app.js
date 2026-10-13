/*
 * AKENO NETWORK console page. Talks only to the console app on 127.0.0.1 (same origin), which
 * holds the device credential and forwards allow-listed API calls to the AKENO server.
 * Server data is always inserted with textContent, never as HTML.
 * Written without optional chaining / nullish coalescing for older WebKit builds.
 */
'use strict';
(function () {
  // ---- Session secret (from the URL fragment the console app opened) ----------------------
  var SESSION_KEY = 'akeno.session';
  var token = '';
  try {
    var m = /[#&]t=([0-9a-f]{64})/.exec(window.location.hash);
    if (m) {
      token = m[1];
      window.sessionStorage.setItem(SESSION_KEY, token);
      window.history.replaceState(null, '', '/');
    } else {
      token = window.sessionStorage.getItem(SESSION_KEY) || '';
    }
  } catch (e) {
    if (m) token = m[1];
  }

  // ---- Small DOM helpers --------------------------------------------------------------------
  function h(tag, props) {
    var node = document.createElement(tag);
    var first = 2;
    // A child in the props position (node, text or list) is treated as a child.
    if (props && (props.nodeType || typeof props === 'string' || Array.isArray(props))) { props = {}; first = 1; }
    props = props || {};
    Object.keys(props).forEach(function (key) {
      var value = props[key];
      if (value === undefined || value === null || value === false) return;
      if (key === 'text') node.textContent = String(value);
      else if (key === 'className') node.className = value;
      else if (key.slice(0, 2) === 'on') node.addEventListener(key.slice(2), value);
      else node.setAttribute(key, value === true ? '' : String(value));
    });
    function add(child) {
      if (child === null || child === undefined || child === false) return;
      if (Array.isArray(child)) child.forEach(add);
      else node.appendChild(typeof child === 'string' ? document.createTextNode(child) : child);
    }
    for (var i = first; i < arguments.length; i++) add(arguments[i]);
    return node;
  }
  function $(id) { return document.getElementById(id); }
  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }
  function initials(name) {
    var parts = String(name || '?').split(/[^A-Za-z0-9]+/).filter(Boolean);
    var text = parts.length > 1 ? parts[0].charAt(0) + parts[1].charAt(0) : (parts[0] || '?').slice(0, 2);
    return text.toUpperCase();
  }
  function presenceLabel(s) { return { online: 'Online', away: 'Away', dnd: 'Do not disturb', offline: 'Offline', invisible: 'Invisible' }[s] || 'Offline'; }
  function timeText(iso) {
    var d = new Date(iso);
    if (isNaN(d.getTime())) return '';
    var today = new Date();
    return d.toDateString() === today.toDateString() ? d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) : d.toLocaleDateString();
  }
  function uuid() {
    var b = new Uint8Array(16);
    window.crypto.getRandomValues(b);
    b[6] = (b[6] & 15) | 64;
    b[8] = (b[8] & 63) | 128;
    var hex = Array.prototype.map.call(b, function (x) { return (x + 256).toString(16).slice(1); }).join('');
    return hex.slice(0, 8) + '-' + hex.slice(8, 12) + '-' + hex.slice(12, 16) + '-' + hex.slice(16, 20) + '-' + hex.slice(20);
  }

  var toastTimer = null;
  function toast(text, bad) {
    var t = $('toast');
    t.textContent = text;
    t.className = 'toast' + (bad ? ' bad' : '');
    t.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.hidden = true; }, 4500);
  }

  // ---- Local API ----------------------------------------------------------------------------
  var appGone = false;
  function api(method, path, body) {
    return new Promise(function (resolve) {
      var xhr = new XMLHttpRequest();
      xhr.open(method, path, true);
      xhr.timeout = 40000;
      xhr.setRequestHeader('X-Akeno-Token', token);
      if (body !== undefined) xhr.setRequestHeader('Content-Type', 'application/json');
      xhr.onload = function () {
        var data;
        try { data = JSON.parse(xhr.responseText); } catch (e) { data = null; }
        if (!data) data = { ok: false, error: { code: 'BAD_RESPONSE', message: 'Unexpected response from the console app.' } };
        if (xhr.status === 403 && data.error && data.error.code === 'SESSION_REQUIRED') showExpired();
        resolve(data);
      };
      xhr.onerror = xhr.ontimeout = function () {
        resolve({ ok: false, offline: true, error: { code: 'APP_UNREACHABLE', message: 'The AKENO console app is not responding.' } });
      };
      xhr.send(body === undefined ? null : JSON.stringify(body));
    });
  }
  // Calls the AKENO server through the console app. Resolves to data or null (error shown).
  function server(method, path, body, quiet) {
    return api(method, '/api/v1' + path, body).then(function (res) {
      if (res.ok) return res.data === undefined ? {} : res.data;
      if (!quiet) toast((res.error && res.error.message) || 'Request failed.', true);
      if (res.error && (res.error.code === 'UNPAIRED' || res.error.code === 'NOT_PAIRED')) refreshState();
      return null;
    });
  }

  // ---- State --------------------------------------------------------------------------------
  var S = {
    app: null, view: 'friends', eventsNext: null,
    friends: [], requests: { incoming: [], outgoing: [] }, conversations: [], invitations: [], notifications: [],
    party: null, parties: [], sessions: [], mine: [], openConversation: null, messages: [], partyMessages: [],
    diagnostics: null, input: { keys: false, gamepad: false, pads: 0 }
  };

  function showExpired() {
    appGone = true;
    $('tabs').hidden = true;
    $('who').hidden = true;
    var main = $('main');
    clear(main);
    main.appendChild(h('section', { className: 'panel center' },
      h('h1', { text: 'This page has expired' }),
      h('p', { className: 'muted', text: 'Start AKENO NETWORK again on your console. It opens a fresh page and keeps your pairing.' })));
  }
  function showGone() {
    appGone = true;
    $('tabs').hidden = true;
    var main = $('main');
    clear(main);
    main.appendChild(h('section', { className: 'panel center' },
      h('h1', { text: 'AKENO NETWORK is not running' }),
      h('p', { className: 'muted', text: 'The console app was closed. You can close this browser page.' })));
  }

  function setLive(rt) {
    var live = $('live');
    var state = (rt && rt.state) || 'idle';
    var cls = state === 'connected' ? 'connected' : state === 'connecting' ? 'connecting' : state === 'idle' ? '' : 'problem';
    live.className = 'live ' + cls;
    var label = { connected: 'Live', connecting: 'Connecting', disconnected: 'Reconnecting', idle: 'Offline', stopped: 'Stopped',
      offline: 'Offline', 'signed-out': 'Signed out', 'not-allowed': 'Live updates off' }[state] || state;
    $('live-text').textContent = label;
    live.title = (rt && rt.detail) || '';
  }

  var lastShape = '';
  function refreshState() {
    return api('GET', '/api/state').then(function (st) {
      if (!st.ok) {
        if (st.offline) showGone();
        return;
      }
      if (appGone) return;
      var wasPaired = S.app && S.app.paired;
      S.app = st;
      if (S.eventsNext === null) S.eventsNext = st.eventsNext;
      setLive(st.paired ? st.realtime : { state: 'idle', detail: '' });
      if (!st.paired) $('live-text').textContent = 'Not paired';
      var shape = (st.configured ? 'c' : '') + (st.paired ? 'p' : '') + ':' + st.pairing.state;
      if (shape !== lastShape) {
        lastShape = shape;
        if (st.paired && !wasPaired) { loadAll(); }
        render();
      } else if (!st.paired && st.pairing.state === 'waiting') {
        var left = $('pair-left');
        if (left) left.textContent = Math.floor(st.pairing.expiresIn / 60) + ':' + ('0' + (st.pairing.expiresIn % 60)).slice(-2);
      }
    });
  }

  // ---- Data loading -------------------------------------------------------------------------
  function loadFriends() {
    return Promise.all([server('GET', '/friends', undefined, true), server('GET', '/friends/requests', undefined, true)]).then(function (r) {
      if (r[0]) S.friends = r[0].friends || [];
      if (r[1]) S.requests = r[1];
      $('count-friends').textContent = S.requests.incoming && S.requests.incoming.length ? String(S.requests.incoming.length) : '';
      if (S.view === 'friends') render();
    });
  }
  function loadConversations() {
    return server('GET', '/conversations', undefined, true).then(function (r) {
      if (!r) return;
      S.conversations = r.conversations || [];
      var unread = S.conversations.reduce(function (n, c) { return n + (c.unreadCount || 0); }, 0);
      $('count-messages').textContent = unread ? String(unread) : '';
      if (S.view === 'messages') render();
    });
  }
  function loadInvitations() {
    return Promise.all([server('GET', '/invitations', undefined, true), server('GET', '/notifications?limit=20', undefined, true)]).then(function (r) {
      if (r[0]) S.invitations = r[0].invitations || [];
      if (r[1]) S.notifications = r[1].notifications || [];
      $('count-invitations').textContent = S.invitations.length ? String(S.invitations.length) : '';
      if (S.view === 'invitations') render();
    });
  }
  function loadParty() {
    return Promise.all([server('GET', '/parties/current', undefined, true), server('GET', '/parties', undefined, true)]).then(function (r) {
      if (r[0]) S.party = r[0].party;
      if (r[1]) S.parties = r[1].parties || [];
      var p = S.party && S.party.isMember && S.party.id ? server('GET', '/parties/' + S.party.id + '/messages?limit=50', undefined, true) : Promise.resolve(null);
      return p.then(function (msgs) {
        S.partyMessages = msgs ? msgs.messages || [] : [];
        if (S.view === 'party') render();
      });
    });
  }
  function loadSessions() {
    return Promise.all([server('GET', '/game-sessions/mine', undefined, true), server('GET', '/game-sessions?limit=30', undefined, true)]).then(function (r) {
      if (r[0]) S.mine = r[0].sessions || [];
      if (r[1]) S.sessions = r[1].sessions || [];
      if (S.view === 'sessions') render();
    });
  }
  function loadMe() {
    return server('GET', '/users/me', undefined, true).then(function (me) {
      if (!me) return;
      S.me = me;
      $('self-name').textContent = (me.profile && me.profile.displayName) || me.onlineId;
      $('presence').value = me.presencePreference === 'invisible' ? 'invisible' : me.presencePreference || 'online';
      $('self-dot').className = 'dot ' + (me.presence || 'online');
    });
  }
  function loadAll() {
    return Promise.all([loadMe(), loadFriends(), loadConversations(), loadInvitations(), loadParty(), loadSessions()]);
  }

  // ---- Rendering ----------------------------------------------------------------------------
  function render() {
    var main = $('main');
    var focusedId = document.activeElement && document.activeElement.id;
    clear(main);
    var st = S.app;
    if (!st) return;
    $('tabs').hidden = !st.paired;
    $('who').hidden = !st.paired;
    if (!st.configured) main.appendChild(viewSetup());
    else if (!st.paired) main.appendChild(viewPairing());
    else {
      Array.prototype.forEach.call(document.querySelectorAll('.tab'), function (t) {
        t.classList.toggle('active', t.getAttribute('data-view') === S.view);
      });
      var views = { friends: viewFriends, messages: viewMessages, invitations: viewInvitations, party: viewParty,
        sessions: viewSessions, diagnostics: viewDiagnostics, settings: viewSettings };
      main.appendChild((views[S.view] || viewFriends)());
    }
    if (focusedId && $(focusedId)) $(focusedId).focus();
    else if (!document.activeElement || document.activeElement === document.body) focusFirst();
  }

  function viewSetup() {
    var server = h('input', { type: 'url', id: 'setup-server', placeholder: 'https://akeno.example.org', value: (S.app && S.app.server) || '' });
    var name = h('input', { type: 'text', id: 'setup-name', maxlength: 48, value: (S.app && S.app.deviceName) || 'PlayStation 5' });
    return h('section', { className: 'panel' },
      h('h1', { text: 'Welcome to AKENO NETWORK' }),
      h('p', { className: 'muted', text: 'Enter the address of your AKENO NETWORK server. The connection is always encrypted and the server certificate is verified.' }),
      h('label', { for: 'setup-server', text: 'Server address' }), server,
      h('label', { for: 'setup-name', text: 'Name of this console (shown when you approve pairing)' }), name,
      h('div', { className: 'row mt' },
        h('button', { className: 'btn', id: 'setup-save', type: 'button', text: 'Continue', onclick: function () {
          api('POST', '/api/settings', { server: server.value.trim(), deviceName: name.value.trim() }).then(function (res) {
            if (!res.ok) return toast(res.error.message, true);
            refreshState();
          });
        } })));
  }

  function viewPairing() {
    var p = S.app.pairing;
    var box = h('section', { className: 'panel center' });
    if (p.state === 'waiting') {
      box.appendChild(h('h1', { text: 'Pair this console' }));
      box.appendChild(h('p', { className: 'muted', text: 'On your phone or PC, sign in to AKENO NETWORK and open:' }));
      box.appendChild(h('p', { className: 'name', text: p.verificationUri }));
      box.appendChild(h('p', { className: 'muted', text: 'Then enter this code:' }));
      box.appendChild(h('div', { className: 'code', id: 'pair-code', text: p.userCode }));
      box.appendChild(h('p', { className: 'muted small' }, 'The code expires in ', h('span', { id: 'pair-left', text: Math.floor(p.expiresIn / 60) + ':' + ('0' + (p.expiresIn % 60)).slice(-2) }),
        '. Only enter codes shown on a console you are holding.'));
      box.appendChild(h('div', { className: 'row centered' },
        h('button', { className: 'btn secondary', id: 'pair-cancel', type: 'button', text: 'Cancel', onclick: function () {
          api('POST', '/api/pair/cancel').then(function () { setTimeout(refreshState, 400); });
        } })));
      return box;
    }
    box.appendChild(h('h1', { text: 'Pair this console with your account' }));
    box.appendChild(h('ol', { className: 'steps' },
      h('li', { text: 'Select "Show pairing code" below.' }),
      h('li', { text: 'On a phone or PC, sign in to your AKENO NETWORK server and enter the code.' }),
      h('li', { text: 'Choose what this console may do, then approve. No password is ever typed on the console.' })));
    var msg = { denied: 'Pairing was declined on the website.', expired: 'The code expired. Show a new one.', cancelled: 'Pairing was cancelled.',
      failed: p.error || 'Pairing failed.' }[p.state];
    if (msg) box.appendChild(h('p', { className: p.state === 'cancelled' ? 'muted' : 'error', text: msg }));
    box.appendChild(h('p', { className: 'muted small', text: 'Server: ' + S.app.server }));
    box.appendChild(h('div', { className: 'row centered mt' },
      h('button', { className: 'btn', id: 'pair-start', type: 'button', text: 'Show pairing code', onclick: function (ev) {
        ev.target.disabled = true;
        api('POST', '/api/pair/start').then(function (res) {
          if (!res.ok) { toast(res.error.message, true); ev.target.disabled = false; }
          refreshState();
        });
      } }),
      h('button', { className: 'btn secondary', id: 'change-server', type: 'button', text: 'Change server', onclick: function () {
        S.app.configured = false;
        lastShape = '';
        clear($('main'));
        $('main').appendChild(viewSetup());
        focusFirst();
      } }),
      h('button', { className: 'btn ghost', id: 'pair-diag', type: 'button', text: 'Diagnostics', onclick: function () {
        clear($('main'));
        $('main').appendChild(viewDiagnostics(true));
        focusFirst();
      } })));
    return box;
  }

  function personRow(user, extra, onclick, id) {
    var presence = user.presence ? user.presence.status : null;
    var activity = user.presence && user.presence.activity;
    var tag = onclick ? 'button' : 'div';
    return h(tag, { className: 'item', type: onclick ? 'button' : null, id: id, onclick: onclick },
      h('span', { className: 'avatar', text: initials(user.displayName || user.onlineId) }, presence ? h('span', { className: 'dot ' + presence }) : null),
      h('span', { className: 'grow' },
        h('div', { className: 'name', text: user.displayName || user.onlineId }),
        h('div', { className: 'sub', text: '@' + user.onlineId + (presence ? ' · ' + presenceLabel(presence) : '') + (activity ? ' · ' + activity : '') })),
      extra || null);
  }

  function openChatWith(user) {
    server('POST', '/conversations', { userId: user.id }).then(function (conv) {
      if (!conv) return;
      S.view = 'messages';
      openConversation(conv);
    });
  }

  function viewFriends() {
    var wrap = h('div');
    var inc = (S.requests && S.requests.incoming) || [];
    if (inc.length) {
      wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Friend requests' }),
        h('div', { className: 'list' }, inc.map(function (r) {
          return personRow(r.user, h('span', { className: 'row' },
            h('button', { className: 'btn', type: 'button', id: 'req-acc-' + r.id, text: 'Accept', onclick: function () {
              server('POST', '/friends/requests/' + r.id + '/accept').then(function (ok) { if (ok) { toast('Friend request accepted.'); loadFriends(); } });
            } }),
            h('button', { className: 'btn secondary', type: 'button', id: 'req-dec-' + r.id, text: 'Decline', onclick: function () {
              server('POST', '/friends/requests/' + r.id + '/decline').then(function (ok) { if (ok) loadFriends(); });
            } })));
        }))));
    }
    var order = { online: 0, dnd: 1, away: 2, offline: 3 };
    var friends = S.friends.slice().sort(function (a, b) {
      var pa = order[a.presence && a.presence.status] === undefined ? 4 : order[a.presence.status];
      var pb = order[b.presence && b.presence.status] === undefined ? 4 : order[b.presence.status];
      return pa - pb || (a.displayName || a.onlineId).localeCompare(b.displayName || b.onlineId);
    });
    var online = friends.filter(function (f) { return f.presence && f.presence.status !== 'offline'; }).length;
    wrap.appendChild(h('section', { className: 'panel' },
      h('h2', { text: 'Friends · ' + online + ' of ' + friends.length + ' online' }),
      friends.length ? h('div', { className: 'list' }, friends.map(function (f) {
        return personRow(f, h('span', { className: 'muted small', text: 'Message' }), function () { openChatWith(f); }, 'friend-' + f.id);
      })) : h('p', { className: 'muted', text: 'No friends yet. Add friends on the AKENO NETWORK website.' })));
    return wrap;
  }

  function openConversation(conv) {
    S.openConversation = conv;
    S.messages = [];
    render();
    server('GET', '/conversations/' + conv.id + '/messages?limit=50').then(function (page) {
      if (!page || !S.openConversation || S.openConversation.id !== conv.id) return;
      S.messages = page.messages || [];
      markRead();
      render();
      var focus = $('composer-input');
      if (focus) focus.focus();
    });
  }
  function markRead() {
    var c = S.openConversation;
    if (!c || !S.messages.length) return;
    var last = S.messages[S.messages.length - 1].id;
    server('POST', '/conversations/' + c.id + '/read', { lastReadMessageId: last }, true).then(function () { loadConversations(); });
  }

  function messageBubbles(list) {
    var me = S.me && S.me.id;
    var box = h('div', { className: 'messages', id: 'message-list' }, list.map(function (msg) {
      var mine = msg.sender && msg.sender.id === me;
      return h('div', { className: 'bubble' + (mine ? ' mine' : '') },
        h('div', { className: 'meta', text: (mine ? 'You' : (msg.sender.displayName || msg.sender.onlineId)) + ' · ' + timeText(msg.createdAt) }),
        h('div', { text: msg.body }));
    }));
    setTimeout(function () { box.scrollTop = box.scrollHeight; }, 0);
    return box;
  }

  function composer(onSend) {
    var input = h('input', { type: 'text', id: 'composer-input', maxlength: 2000, placeholder: 'Write a message' });
    function send() {
      var text = input.value.trim();
      if (!text) return;
      input.disabled = true;
      onSend(text).then(function (ok) {
        input.disabled = false;
        if (ok) input.value = '';
        input.focus();
      });
    }
    input.addEventListener('keydown', function (ev) { if (ev.key === 'Enter') { ev.preventDefault(); send(); } });
    return h('div', { className: 'composer' }, input, h('button', { className: 'btn', type: 'button', id: 'composer-send', text: 'Send', onclick: send }));
  }

  function viewMessages() {
    var list = h('section', { className: 'panel' }, h('h2', { text: 'Conversations' }),
      S.conversations.length ? h('div', { className: 'list' }, S.conversations.map(function (c) {
        var last = c.lastMessage ? c.lastMessage.body : 'No messages yet';
        var row = personRow(c.otherUser, c.unreadCount ? h('span', { className: 'count', text: String(c.unreadCount) }) : null,
          function () { openConversation(c); }, 'conv-' + c.id);
        row.querySelector('.sub').textContent = last;
        if (S.openConversation && S.openConversation.id === c.id) row.classList.add('selected');
        return row;
      })) : h('p', { className: 'muted', text: 'No conversations yet. Pick a friend to start one.' }));
    var c = S.openConversation;
    var chat = h('section', { className: 'panel chat' });
    if (!c) {
      chat.appendChild(h('p', { className: 'muted', text: 'Select a conversation.' }));
    } else {
      chat.appendChild(h('h2', { text: c.otherUser.displayName || c.otherUser.onlineId }));
      chat.appendChild(messageBubbles(S.messages));
      if (c.canSend === false) chat.appendChild(h('p', { className: 'muted', text: 'You cannot send messages in this conversation.' }));
      else chat.appendChild(composer(function (text) {
        return server('POST', '/conversations/' + c.id + '/messages', { body: text, clientMessageId: uuid() }).then(function (res) {
          if (!res) return false;
          if (res.message) addMessage(S.messages, res.message);
          render();
          return true;
        });
      }));
    }
    return h('div', { className: 'split' }, list, chat);
  }

  function addMessage(list, msg) {
    for (var i = 0; i < list.length; i++) if (list[i].id === msg.id) return false;
    list.push(msg);
    return true;
  }

  function viewInvitations() {
    var wrap = h('div');
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Invitations' }),
      S.invitations.length ? h('div', { className: 'list' }, S.invitations.map(function (inv) {
        var kind = { party: 'Party', game_session: 'Game session', group: 'Group' }[inv.kind] || inv.kind;
        var row = personRow(inv.inviter, h('span', { className: 'row' },
          h('button', { className: 'btn', type: 'button', id: 'inv-acc-' + inv.id, text: 'Accept', onclick: function () {
            server('POST', '/invitations/' + inv.id + '/accept').then(function (ok) {
              if (!ok) return;
              toast('Invitation accepted.');
              loadInvitations();
              if (inv.kind === 'party') { loadParty(); S.view = 'party'; render(); }
              if (inv.kind === 'game_session') loadSessions();
            });
          } }),
          h('button', { className: 'btn secondary', type: 'button', id: 'inv-dec-' + inv.id, text: 'Decline', onclick: function () {
            server('POST', '/invitations/' + inv.id + '/decline').then(function (ok) { if (ok) loadInvitations(); });
          } })));
        row.querySelector('.sub').textContent = kind + ': ' + inv.target.name + ' · from @' + inv.inviter.onlineId;
        return row;
      })) : h('p', { className: 'muted', text: 'No pending invitations.' })));
    var texts = {
      'friend_request.received': 'sent you a friend request', 'friend_request.accepted': 'accepted your friend request',
      'invitation.received': 'invited you', 'game_session.join_requested': 'asked to join your game session',
      'game_session.request_accepted': 'accepted your request to join', 'game_session.request_rejected': 'declined your request to join',
      'group.removed': 'removed you from a group', 'group.role_changed': 'changed your role in a group'
    };
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Recent activity' }),
      S.notifications.length ? h('div', { className: 'list' }, S.notifications.map(function (n) {
        var actor = n.data.actor || { onlineId: '?' };
        return h('div', { className: 'item' }, h('span', { className: 'grow' },
          h('div', { text: (actor.displayName || actor.onlineId) + ' ' + (texts[n.data.type] || n.data.type) +
            (n.data.targetName ? ': ' + n.data.targetName : n.data.gameName ? ': ' + n.data.gameName : '') }),
          h('div', { className: 'sub', text: timeText(n.createdAt) })));
      })) : h('p', { className: 'muted', text: 'Nothing new.' })));
    return wrap;
  }

  function viewParty() {
    var wrap = h('div');
    var p = S.party;
    if (p && p.isMember) {
      var party = h('section', { className: 'panel' },
        h('div', { className: 'row' }, h('h2', { text: 'Party: ' + (p.name || 'Party') }), h('span', { className: 'spacer' }),
          h('button', { className: 'btn secondary', type: 'button', id: 'party-leave', text: 'Leave party', onclick: function () {
            server('POST', '/parties/' + p.id + '/leave').then(function (ok) { if (ok) { toast('You left the party.'); loadParty(); } });
          } })),
        h('p', { className: 'muted small', text: p.memberCount + ' of ' + p.maxMembers + ' members · ' + (p.privacy === 'friends' ? 'Friends can join' : 'Invite only') +
          ' · Text chat only (' + ((p.voice && p.voice.reason) || 'voice chat is not available') + ')' }),
        h('div', { className: 'list' }, (p.members || []).map(function (m) {
          return personRow(m.user, m.isLeader ? h('span', { className: 'badge', text: 'Leader' }) : null);
        })));
      wrap.appendChild(party);
      var chat = h('section', { className: 'panel chat' }, h('h2', { text: 'Party chat' }), messageBubbles(S.partyMessages),
        composer(function (text) {
          return server('POST', '/parties/' + p.id + '/messages', { body: text, clientMessageId: uuid() }).then(function (res) {
            if (!res) return false;
            if (res.message) addMessage(S.partyMessages, res.message);
            render();
            return true;
          });
        }));
      wrap.appendChild(chat);
    } else {
      wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'You are not in a party' }),
        h('p', { className: 'muted', text: 'Parties on AKENO NETWORK are text-only. Create one or join a friend\'s party below.' }),
        h('button', { className: 'btn', type: 'button', id: 'party-create', text: 'Create a party for friends', onclick: function () {
          server('POST', '/parties', { privacy: 'friends', maxMembers: 8 }).then(function (ok) { if (ok) { toast('Party created.'); loadParty(); } });
        } })));
    }
    var joinable = S.parties.filter(function (x) { return !(p && p.id === x.id && p.isMember); });
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Parties you can join' }),
      joinable.length ? h('div', { className: 'list' }, joinable.map(function (x) {
        return h('div', { className: 'item' }, h('span', { className: 'grow' },
          h('div', { className: 'name', text: x.name || 'Party' }),
          h('div', { className: 'sub', text: x.memberCount + ' of ' + x.maxMembers + ' members' })),
          h('button', { className: 'btn', type: 'button', id: 'party-join-' + x.id, text: 'Join', onclick: function () {
            server('POST', '/parties/' + x.id + '/join').then(function (ok) { if (ok) { toast('Joined the party.'); loadParty(); } });
          } }));
      })) : h('p', { className: 'muted', text: 'No open parties from your friends right now.' })));
    return wrap;
  }

  function sessionRows(list) {
    return h('div', { className: 'list' }, list.map(function (s) {
      return h('div', { className: 'item' }, h('span', { className: 'grow' },
        h('div', { className: 'name', text: s.gameName }),
        h('div', { className: 'sub', text: [s.status, s.platform, s.region, (s.participantCount || 0) + '/' + s.maxParticipants + ' players',
          s.host ? 'host @' + s.host.onlineId : ''].filter(Boolean).join(' · ') })));
    }));
  }

  function viewSessions() {
    return h('div', {},
      h('section', { className: 'panel' }, h('h2', { text: 'My game sessions' }),
        S.mine.length ? sessionRows(S.mine) : h('p', { className: 'muted', text: 'You are not in any game session.' })),
      h('section', { className: 'panel' }, h('h2', { text: 'Open game sessions' }),
        h('p', { className: 'muted small', text: 'Sessions are arranged on AKENO NETWORK; you meet inside the game using the game\'s own systems.' }),
        S.sessions.length ? sessionRows(S.sessions) : h('p', { className: 'muted', text: 'No open sessions.' })));
  }

  function kv(pairs) {
    return h('dl', { className: 'kv' }, pairs.map(function (p) { return [h('dt', { text: p[0] }), h('dd', { text: p[1] === undefined || p[1] === null ? '' : String(p[1]) })]; }));
  }

  function viewDiagnostics(standalone) {
    var wrap = h('div');
    var d = S.diagnostics;
    var result = h('p', { id: 'conn-result', className: 'muted', text: '' });
    wrap.appendChild(h('section', { className: 'panel' },
      h('div', { className: 'row' }, h('h2', { text: 'Diagnostics' }), h('span', { className: 'spacer' }),
        h('button', { className: 'btn', type: 'button', id: 'diag-test', text: 'Test connection', onclick: function (ev) {
          ev.target.disabled = true;
          result.textContent = 'Testing...';
          api('POST', '/api/diagnostics/connection').then(function (res) {
            ev.target.disabled = false;
            result.className = res.ok ? 'ok' : 'error';
            result.textContent = (res.ok ? 'OK: ' : 'Failed: ') + (res.detail || (res.error && res.error.message) || '') +
              (res.tls ? ' (' + res.tls + ')' : '') + (res.milliseconds !== undefined ? ' in ' + res.milliseconds + ' ms' : '');
          });
        } }),
        h('button', { className: 'btn secondary', type: 'button', id: 'diag-reconnect', text: 'Reconnect live updates', onclick: function () {
          api('POST', '/api/realtime/reconnect').then(function () { toast('Reconnecting live updates.'); setTimeout(refreshState, 800); });
        } }),
        h('button', { className: 'btn secondary', type: 'button', id: 'diag-refresh', text: 'Refresh', onclick: loadDiagnostics }),
        standalone ? h('button', { className: 'btn ghost', type: 'button', id: 'diag-back', text: 'Back', onclick: function () { lastShape = ''; refreshState(); } }) : null),
      result));
    if (!d) {
      wrap.appendChild(h('p', { className: 'muted', text: 'Loading...' }));
      loadDiagnostics(standalone);
      return wrap;
    }
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Console app' }), kv([
      ['Version', d.version + ' (' + d.build + ')'], ['Platform', d.platform], ['Firmware', (d.firmware || 'unknown') + ' — ' + d.firmwareNote],
      ['Running for', d.uptimeSeconds + ' s'], ['Server', d.server || 'not set'], ['Data folder', d.dataDir],
      ['Device credential', d.credential.present ? 'present (mode ' + d.credential.mode + ')' : 'none'],
      ['Live updates', d.realtime.state + (d.realtime.detail ? ' — ' + d.realtime.detail : '')]])));
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Secure connection' }), kv([
      ['TLS library', d.tls.backend], ['Trusted roots', d.tls.trustStore + (d.tls.trustedRoots >= 0 ? ' (' + d.tls.trustedRoots + ' loaded)' : '')],
      ['Verification', d.tls.verification], ['Random numbers', d.tls.entropy]])));
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Console integration' }), kv([
      ['Page', 'http://127.0.0.1:' + d.ui.port + ' (this console only)'], ['Browser', d.ui.browserMethod + ' · launches: ' + d.ui.browserLaunches + ', last result: ' + d.ui.browserLastResult],
      ['Notifications', d.notifications.method + ' · sent ' + d.notifications.sent + ', failed ' + d.notifications.failed + ', rate-limited ' + d.notifications.dropped],
      ['Controller input', (S.input.keys ? 'arrow keys seen' : 'no arrow keys seen yet') + ' · Gamepad API ' + (navigator.getGamepads ? 'available' : 'not available') +
        (S.input.pads ? ' (' + S.input.pads + ' connected)' : '')],
      ['Browser engine', navigator.userAgent]])));
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Features' }),
      h('dl', { className: 'kv' }, d.capabilities.map(function (c) {
        return [h('dt', { text: c.feature }), h('dd', {}, h('span', { className: 'state-' + c.state, text: c.state }), ' — ' + c.reason)];
      }))));
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Recent log (tokens are redacted)' }),
      h('div', { className: 'log mono', text: d.log.join('\n') })));
    return wrap;
  }
  function loadDiagnostics(standalone) {
    api('GET', '/api/diagnostics').then(function (d) {
      if (!d.ok) return;
      S.diagnostics = d;
      if (standalone === true || !(S.app && S.app.paired)) { clear($('main')); $('main').appendChild(viewDiagnostics(true)); focusFirst(); }
      else if (S.view === 'diagnostics') render();
    });
  }

  function viewSettings() {
    var n = S.app.notifications;
    function toggle(id, label, key, value) {
      var box = h('input', { type: 'checkbox', id: id });
      box.checked = !!value;
      box.addEventListener('change', function () {
        var body = {};
        body[key] = box.checked;
        api('POST', '/api/settings', body).then(function (res) { if (!res.ok) toast(res.error.message, true); refreshState(); });
      });
      return h('label', { className: 'check', for: id }, box, label);
    }
    return h('div', {},
      h('section', { className: 'panel' }, h('h2', { text: 'Notifications' }),
        h('p', { className: 'muted small', text: 'Shown as system notifications while AKENO NETWORK runs, also when this page is closed.' }),
        toggle('set-notify-messages', 'New messages', 'notifyMessages', n.messages),
        toggle('set-notify-social', 'Friend requests and invitations', 'notifySocial', n.social)),
      h('section', { className: 'panel' }, h('h2', { text: 'This console' }), kv([
        ['Paired as', '@' + S.app.onlineId], ['Console name', S.app.deviceName], ['Server', S.app.server]]),
        h('div', { className: 'row mt' },
          h('button', { className: 'btn danger', type: 'button', id: 'set-unpair', text: 'Unpair this console', onclick: function () {
            if (!window.confirm('Unpair this console? It signs out here and on the server. You can pair again later.')) return;
            api('POST', '/api/logout').then(function () { toast('This console was unpaired.'); lastShape = ''; refreshState(); });
          } }))),
      h('section', { className: 'panel' }, h('h2', { text: 'About' }),
        h('p', { className: 'muted small', text: 'AKENO NETWORK ' + S.app.app.version + ' (' + S.app.app.build + '). An independent, self-hosted social network for ' +
          'your friends. It does not use, modify or replace PlayStation Network features; the console\'s own friends list, parties and voice chat are unchanged.' })));
  }

  // ---- Realtime events ----------------------------------------------------------------------
  function handleEvent(e) {
    var d = e.data || {};
    switch (e.type) {
      case 'realtime.state':
        if (S.app) { S.app.realtime = d; setLive(d); }
        if (d.state === 'connected') loadAll(); // Catch up on anything missed while disconnected.
        break;
      case 'message.created':
        if (d.conversationKind === 'party') {
          if (S.party && S.party.conversationId === d.conversationId && d.message && addMessage(S.partyMessages, d.message) && S.view === 'party') render();
        } else if (S.openConversation && S.openConversation.id === d.conversationId) {
          if (d.message && addMessage(S.messages, d.message)) { markRead(); if (S.view === 'messages') render(); }
        } else {
          loadConversations();
        }
        break;
      case 'presence.updated':
        S.friends.forEach(function (f) { if (f.id === d.userId) f.presence = d.presence; });
        if (S.view === 'friends') render();
        break;
      case 'self.presence':
        $('self-dot').className = 'dot ' + d.status;
        break;
      case 'friends.changed': loadFriends(); break;
      case 'notification.created': case 'invitations.changed': loadInvitations(); break;
      case 'party.changed': loadParty(); break;
      case 'game-session.changed': loadSessions(); break;
      case 'conversation.read': loadConversations(); break;
      default: break;
    }
  }
  function pollEvents() {
    if (appGone || !S.app || !S.app.paired || S.eventsNext === null) return Promise.resolve();
    return api('GET', '/api/events?after=' + S.eventsNext).then(function (res) {
      if (!res.ok) return;
      S.eventsNext = res.next;
      if (res.gap) loadAll();
      (res.events || []).forEach(handleEvent);
    });
  }

  // ---- Controller and keyboard navigation ---------------------------------------------------
  function focusables() {
    return Array.prototype.filter.call(document.querySelectorAll('button, input, select, textarea, [tabindex="0"]'), function (el) {
      if (el.disabled || el.hidden) return false;
      var r = el.getBoundingClientRect();
      return r.width > 0 && r.height > 0;
    });
  }
  function focusFirst() {
    var main = focusables().filter(function (el) { return $('main').contains(el); });
    if (main.length) main[0].focus();
  }
  function move(dir) {
    var list = focusables();
    var cur = document.activeElement;
    if (!cur || list.indexOf(cur) < 0) { if (list.length) list[0].focus(); return; }
    var a = cur.getBoundingClientRect();
    var ax = a.left + a.width / 2, ay = a.top + a.height / 2;
    var best = null, bestScore = Infinity;
    list.forEach(function (el) {
      if (el === cur) return;
      var b = el.getBoundingClientRect();
      var bx = b.left + b.width / 2, by = b.top + b.height / 2;
      var dx = bx - ax, dy = by - ay;
      var primary = dir === 'left' ? -dx : dir === 'right' ? dx : dir === 'up' ? -dy : dy;
      if (primary <= 4) return;
      var ortho = dir === 'left' || dir === 'right' ? Math.abs(dy) : Math.abs(dx);
      var score = primary + ortho * 2.5;
      if (score < bestScore) { bestScore = score; best = el; }
    });
    if (best) {
      best.focus();
      if (best.scrollIntoView) best.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    }
  }
  function back() {
    if (S.view === 'messages' && S.openConversation) { S.openConversation = null; render(); return; }
    if (S.app && S.app.paired && S.view !== 'friends') { S.view = 'friends'; render(); focusFirst(); }
  }
  var lastKeyNav = 0;
  document.addEventListener('keydown', function (ev) {
    var dirs = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' };
    var t = ev.target;
    var typing = t && (t.tagName === 'INPUT' && t.type !== 'checkbox' || t.tagName === 'TEXTAREA');
    var selecting = t && t.tagName === 'SELECT';
    if (dirs[ev.key] && !(typing && (ev.key === 'ArrowLeft' || ev.key === 'ArrowRight')) && !(selecting && (ev.key === 'ArrowUp' || ev.key === 'ArrowDown'))) {
      S.input.keys = true;
      lastKeyNav = Date.now();
      ev.preventDefault();
      move(dirs[ev.key]);
    } else if ((ev.key === 'Escape' || ev.key === 'Backspace') && !typing) {
      ev.preventDefault();
      back();
    }
  });
  // Gamepad API (used only if the browser exposes it): D-pad moves focus, Cross selects, Circle goes back.
  var padPrev = {};
  function pollPads() {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    var count = 0;
    for (var i = 0; i < pads.length; i++) {
      var p = pads[i];
      if (!p || !p.connected) continue;
      count++;
      var map = { 12: 'up', 13: 'down', 14: 'left', 15: 'right', 0: 'select', 1: 'back' };
      Object.keys(map).forEach(function (k) {
        var pressed = p.buttons[k] && p.buttons[k].pressed;
        var key = i + ':' + k;
        if (pressed && !padPrev[key] && Date.now() - lastKeyNav > 400) {
          S.input.gamepad = true;
          var a = map[k];
          if (a === 'select') { if (document.activeElement && document.activeElement.click) document.activeElement.click(); }
          else if (a === 'back') back();
          else move(a);
        }
        padPrev[key] = pressed;
      });
    }
    S.input.pads = count;
    window.requestAnimationFrame(pollPads);
  }
  if (navigator.getGamepads && window.requestAnimationFrame) window.requestAnimationFrame(pollPads);

  // ---- Wiring -------------------------------------------------------------------------------
  Array.prototype.forEach.call(document.querySelectorAll('.tab'), function (tab) {
    tab.addEventListener('click', function () {
      S.view = tab.getAttribute('data-view');
      if (S.view === 'diagnostics') { S.diagnostics = null; }
      if (S.view === 'friends') loadFriends();
      if (S.view === 'messages') loadConversations();
      if (S.view === 'invitations') loadInvitations();
      if (S.view === 'party') loadParty();
      if (S.view === 'sessions') loadSessions();
      render();
      tab.focus();
    });
  });
  $('presence').addEventListener('change', function () {
    server('PUT', '/presence', { status: $('presence').value }).then(function (res) {
      if (res) { toast('Status: ' + presenceLabel($('presence').value)); $('self-dot').className = 'dot ' + res.status; }
    });
  });
  $('exit-btn').addEventListener('click', function () {
    if (!window.confirm('Exit AKENO NETWORK? You will stop receiving AKENO notifications until you start it again.')) return;
    api('POST', '/api/exit').then(function () { showGone(); });
  });

  if (!token) {
    showExpired();
  } else {
    refreshState().then(function () { if (S.app && S.app.paired) loadAll(); });
    setInterval(function () { if (!appGone) refreshState(); }, 3000);
    setInterval(function () { if (!appGone) pollEvents(); }, 2000);
  }
})();
