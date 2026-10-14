/*
 * AKENO NETWORK console page. Talks only to the console app on 127.0.0.1 (same origin), which
 * holds the device credential and forwards allow-listed API calls to the AKENO server.
 * Server data is always inserted with textContent, never as HTML.
 * Written without optional chaining / nullish coalescing for older WebKit builds.
 */
'use strict';
(function () {
  // ---- Session secret and quick-open target (from the URL fragment the console app opened) -----
  // "#t=<secret>[&open=<target>]". The target names a view or a conversation; the AKENO server
  // still decides whether this account may open that conversation.
  var SESSION_KEY = 'akeno.session';
  var OPEN_PATTERN = /^(messages|party|friends|games|invitations|conversation:[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$/;
  var token = '';
  var pendingOpen = '';
  function readFragment() {
    var hash = window.location.hash || '';
    var m = /[#&]t=([0-9a-f]{64})/.exec(hash);
    var o = /[#&]open=([a-z0-9:-]+)/.exec(hash);
    if (o && OPEN_PATTERN.test(o[1])) pendingOpen = o[1];
    if (m) {
      token = m[1];
      try { window.sessionStorage.setItem(SESSION_KEY, token); } catch (e) { /* storage unavailable */ }
    } else if (!token) {
      try { token = window.sessionStorage.getItem(SESSION_KEY) || ''; } catch (e) { token = ''; }
    }
    if (m || o) {
      try { window.history.replaceState(null, '', '/'); } catch (e) { /* older browsers keep the fragment */ }
    }
  }
  readFragment();

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
  function durationText(seconds) {
    var s = Math.max(0, Math.floor(seconds || 0));
    if (s < 60) return 'under a minute';
    var minutes = Math.floor(s / 60);
    if (minutes < 60) return minutes + ' min';
    var hours = Math.floor(minutes / 60), rest = minutes % 60;
    return rest ? hours + ' h ' + rest + ' min' : hours + ' h';
  }
  function secondsSince(iso) {
    var t = new Date(iso).getTime();
    return isNaN(t) ? 0 : Math.max(0, Math.floor((Date.now() - t) / 1000));
  }
  function dayText(iso) {
    var d = new Date(iso);
    if (isNaN(d.getTime())) return '';
    var today = new Date();
    var yesterday = new Date(today.getTime() - 86400000);
    if (d.toDateString() === today.toDateString()) return 'today';
    if (d.toDateString() === yesterday.toDateString()) return 'yesterday';
    return d.toLocaleDateString();
  }
  function gameName(g) { return (g && (g.name || g.titleId)) || 'a game'; }
  function isTyping() {
    var t = document.activeElement;
    return !!t && (t.tagName === 'TEXTAREA' || (t.tagName === 'INPUT' && t.type !== 'checkbox'));
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

  // ---- Images -------------------------------------------------------------------------------
  // Profile pictures and game artwork come from the AKENO server through the console app; icons
  // of games installed on this console are read from its own metadata (read-only, never sent
  // anywhere). Both arrive as small JSON documents and are shown as data: URLs, so the page keeps
  // its strict policy (images only from itself or data:). Failures simply keep the initials.
  var IMAGE_CACHE = {};
  var IMAGE_WAITERS = {};
  var IMAGE_ORDER = [];
  var IMAGE_CACHE_MAX = 60;
  function cacheImage(key, url) {
    if (!(key in IMAGE_CACHE)) IMAGE_ORDER.push(key);
    IMAGE_CACHE[key] = url;
    while (IMAGE_ORDER.length > IMAGE_CACHE_MAX) delete IMAGE_CACHE[IMAGE_ORDER.shift()];
  }
  function loadImage(key, path, done) {
    if (key in IMAGE_CACHE) { done(IMAGE_CACHE[key]); return; }
    if (IMAGE_WAITERS[key]) { IMAGE_WAITERS[key].push(done); return; }
    IMAGE_WAITERS[key] = [done];
    api('GET', path).then(function (res) {
      var ok = res.ok && /^image\/(png|jpeg|webp)$/.test(res.type || '') && /^[A-Za-z0-9+/=]+$/.test(res.data || '');
      var url = ok ? 'data:' + res.type + ';base64,' + res.data : false;
      // Missing images are remembered too (no retry storm); network failures are not.
      if (ok || !res.offline) cacheImage(key, url);
      var waiters = IMAGE_WAITERS[key] || [];
      delete IMAGE_WAITERS[key];
      waiters.forEach(function (cb) { cb(url); });
    });
  }
  function fillImage(holder, key, path) {
    loadImage(key, path, function (url) {
      if (!url || !holder) return;
      var img = h('img', { src: url, alt: '', draggable: 'false' });
      holder.textContent = '';
      holder.insertBefore(img, holder.firstChild);
      holder.className += ' has-image';
    });
  }
  function mediaPath(id) { return '/api/media?id=' + id + '&v=fallback'; }
  function avatarNode(user, presence) {
    var node = h('span', { className: 'avatar', text: initials(user.displayName || user.onlineId) });
    if (presence) node.appendChild(h('span', { className: 'dot ' + presence }));
    if (user.avatarImage && /^[0-9a-f]{32}$/.test(user.avatarImage)) {
      fillImage(node, 'm:' + user.avatarImage, mediaPath(user.avatarImage));
    }
    return node;
  }

  // ---- State --------------------------------------------------------------------------------
  var S = {
    app: null, view: 'friends', eventsNext: null,
    friends: [], requests: { incoming: [], outgoing: [] }, conversations: [], invitations: [], notifications: [],
    party: null, parties: [], sessions: [], mine: [], openConversation: null, messages: [], partyMessages: [],
    diagnostics: null, input: { keys: false, gamepad: false, pads: 0 },
    // Screens shown over the pairing view ('setup' = change server, 'diagnostics'); kept until the
    // user saves or cancels, whatever the periodic state refresh reports.
    overlay: null, setupDraft: null,
    drafts: {}, myActivity: null, bannerDismissed: 0
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
      var before = S.app;
      S.app = st;
      if (S.eventsNext === null) S.eventsNext = st.eventsNext;
      if (S.overlay === 'setup') return; // Never disturb the server form while it is open.
      setLive(st.paired ? st.realtime : { state: 'idle', detail: '' });
      if (!st.paired) $('live-text').textContent = 'Not paired';
      var shape = (st.configured ? 'c' : '') + (st.paired ? 'p' : '') + ':' + st.pairing.state;
      if (shape !== lastShape) {
        lastShape = shape;
        if (st.paired && !wasPaired) { loadAll().then(applyOpen); }
        render();
      } else if (!st.paired && st.pairing.state === 'waiting') {
        var left = $('pair-left');
        if (left) left.textContent = Math.floor(st.pairing.expiresIn / 60) + ':' + ('0' + (st.pairing.expiresIn % 60)).slice(-2);
      } else if (st.paired && S.view === 'games' && before && activityKey(before.activity) !== activityKey(st.activity) && !isTyping()) {
        render();
      }
      renderBanner();
    });
  }
  function activityKey(a) { return a ? [a.enabled, a.state, a.titleId, a.name, a.startedAt].join('|') : ''; }

  // ---- Quick access: newest message banner and "open" targets ------------------------------
  function markInboxSeen(seq) {
    var inbox = S.app && S.app.inbox;
    if (!inbox || !seq || seq <= inbox.seenSeq) return;
    inbox.seenSeq = seq;
    inbox.unseen = 0;
    api('POST', '/api/inbox/seen', { seq: seq });
    renderBanner();
  }
  function renderBanner() {
    var box = $('banner');
    if (!box) return;
    var inbox = S.app && S.app.paired && S.app.inbox;
    var latest = inbox && inbox.latest;
    var show = !!latest && inbox.unseen > 0 && latest.seq > inbox.seenSeq && latest.seq > S.bannerDismissed && !appGone;
    if (!show) { if (!box.hidden) { clear(box); box.hidden = true; } return; }
    if (box.getAttribute('data-seq') === String(latest.seq) && !box.hidden) return;
    var hadFocus = box.contains(document.activeElement);
    clear(box);
    box.setAttribute('data-seq', String(latest.seq));
    box.hidden = false;
    box.appendChild(h('div', { className: 'grow' },
      h('div', { className: 'name', text: 'New message from ' + latest.from + (latest.kind === 'party' ? ' (party)' : latest.kind === 'group' ? ' (group)' : '') }),
      h('div', { className: 'sub', text: latest.preview })));
    box.appendChild(h('button', { className: 'btn', id: 'banner-open', type: 'button', text: 'Open', onclick: function () {
      markInboxSeen(latest.seq);
      openTarget(latest.open);
    } }));
    box.appendChild(h('button', { className: 'btn ghost', id: 'banner-dismiss', type: 'button', text: 'Dismiss', onclick: function () {
      S.bannerDismissed = latest.seq;
      markInboxSeen(latest.seq);
      focusFirst();
    } }));
    if (hadFocus) $('banner-open').focus();
  }
  // Coming back to the page (e.g. from a game): offer the newest message first, without
  // interrupting typing.
  function onReturn() {
    if (appGone || !token) return;
    refreshState().then(function () {
      var open = $('banner-open');
      if (open && !$('banner').hidden && !isTyping()) open.focus();
    });
  }
  function openTarget(target) {
    if (!target || !OPEN_PATTERN.test(target) || !S.app || !S.app.paired) return;
    S.overlay = null;
    if (target.indexOf('conversation:') === 0) {
      var id = target.slice(13);
      S.view = 'messages';
      var known = null;
      S.conversations.forEach(function (c) { if (c.id === id) known = c; });
      if (known) openConversation(known);
      else server('GET', '/conversations/' + id).then(function (conv) { if (conv) openConversation(conv); else render(); });
      loadConversations();
      return;
    }
    S.view = target;
    var loaders = { messages: loadConversations, party: loadParty, friends: loadFriends, games: loadActivity, invitations: loadInvitations };
    if (loaders[target]) loaders[target]();
    render();
    focusFirst();
  }
  function applyOpen() {
    var target = pendingOpen;
    pendingOpen = '';
    if (!target) return;
    var inbox = S.app && S.app.inbox;
    if (inbox && inbox.latest && inbox.latest.open === target) markInboxSeen(inbox.latest.seq);
    openTarget(target);
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
  function loadActivity() {
    if (!S.me) return Promise.resolve();
    return server('GET', '/users/' + S.me.id + '/activity', undefined, true).then(function (r) {
      if (r) S.myActivity = r;
      if (S.view === 'games' && !isTyping()) render();
    });
  }
  function loadAll() {
    return Promise.all([loadMe().then(loadActivity), loadFriends(), loadConversations(), loadInvitations(), loadParty(), loadSessions()]);
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
    if (!st.configured || S.overlay === 'setup') main.appendChild(viewSetup());
    else if (!st.paired) main.appendChild(S.overlay === 'diagnostics' ? viewDiagnostics(true) : viewPairing());
    else {
      Array.prototype.forEach.call(document.querySelectorAll('.tab'), function (t) {
        t.classList.toggle('active', t.getAttribute('data-view') === S.view);
      });
      var views = { friends: viewFriends, messages: viewMessages, games: viewGames, invitations: viewInvitations, party: viewParty,
        sessions: viewSessions, diagnostics: viewDiagnostics, settings: viewSettings };
      main.appendChild((views[S.view] || viewFriends)());
    }
    var again = focusedId && $(focusedId);
    if (again) {
      again.focus();
      if (again.tagName === 'INPUT' && again.type === 'text' && again.setSelectionRange) {
        try { again.setSelectionRange(again.value.length, again.value.length); } catch (e) { /* not a text field */ }
      }
    } else if (!document.activeElement || document.activeElement === document.body) {
      focusFirst();
    }
    renderBanner();
  }

  // First run, or "Change server" from the pairing screen. What is typed lives in S.setupDraft,
  // so re-rendering never loses it; nothing is saved until the user selects Save.
  function viewSetup() {
    var st = S.app;
    var changing = st.configured;
    if (!S.setupDraft) S.setupDraft = { server: st.server || '', name: st.deviceName || 'PlayStation 5' };
    var draft = S.setupDraft;
    var server = h('input', { type: 'url', id: 'setup-server', placeholder: 'https://akeno.example.org', value: draft.server });
    var name = h('input', { type: 'text', id: 'setup-name', maxlength: 48, value: draft.name });
    server.value = draft.server;
    name.value = draft.name;
    server.addEventListener('input', function () { draft.server = server.value; });
    name.addEventListener('input', function () { draft.name = name.value; });
    function close() {
      S.overlay = null;
      S.setupDraft = null;
      lastShape = '';
    }
    return h('section', { className: 'panel' },
      h('h1', { text: changing ? 'Change server' : 'Welcome to AKENO NETWORK' }),
      h('p', { className: 'muted', text: 'Enter the address of your AKENO NETWORK server. The connection is always encrypted and the server certificate is verified.' }),
      changing ? h('p', { className: 'muted small', text: 'Currently: ' + st.server + '. Nothing changes until you select Save.' }) : null,
      h('label', { for: 'setup-server', text: 'Server address' }), server,
      h('label', { for: 'setup-name', text: 'Name of this console (shown when you approve pairing)' }), name,
      h('div', { className: 'row mt' },
        h('button', { className: 'btn', id: 'setup-save', type: 'button', text: changing ? 'Save' : 'Continue', onclick: function () {
          api('POST', '/api/settings', { server: draft.server.trim(), deviceName: draft.name.trim() }).then(function (res) {
            if (!res.ok) return toast(res.error.message, true);
            close();
            refreshState();
          });
        } }),
        changing ? h('button', { className: 'btn secondary', id: 'setup-cancel', type: 'button', text: 'Cancel', onclick: function () {
          close();
          render();
          focusFirst();
        } }) : null));
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
        S.overlay = 'setup';
        S.setupDraft = null;
        render();
        focusFirst();
      } }),
      h('button', { className: 'btn ghost', id: 'pair-diag', type: 'button', text: 'Diagnostics', onclick: function () {
        S.overlay = 'diagnostics';
        S.diagnostics = null;
        render();
        focusFirst();
      } })));
    return box;
  }

  function personRow(user, extra, onclick, id) {
    var presence = user.presence ? user.presence.status : null;
    var activity = user.presence && user.presence.activity;
    var game = user.presence && user.presence.game;
    var tag = onclick ? 'button' : 'div';
    return h(tag, { className: 'item', type: onclick ? 'button' : null, id: id, onclick: onclick },
      avatarNode(user, presence),
      h('span', { className: 'grow' },
        h('div', { className: 'name', text: user.displayName || user.onlineId }),
        h('div', { className: 'sub', text: '@' + user.onlineId + (presence ? ' · ' + presenceLabel(presence) : '') + (activity && !game ? ' · ' + activity : '') }),
        game ? h('div', { className: 'playing' }, gameIcon(gameName(game), game.titleId, game.art, true),
          h('span', { text: 'Playing ' + gameName(game) + ' · PlayStation 5 · ' + durationText(secondsSince(game.startedAt)) })) : null),
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
    var inbox = S.app && S.app.inbox;
    if (inbox && inbox.latest && inbox.latest.open === 'conversation:' + conv.id) markInboxSeen(inbox.latest.seq);
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

  // The text being written survives re-renders (new messages, presence updates) per conversation.
  function composer(key, onSend) {
    var input = h('input', { type: 'text', id: 'composer-input', maxlength: 2000, placeholder: 'Write a message' });
    input.value = S.drafts[key] || '';
    input.addEventListener('input', function () { S.drafts[key] = input.value; });
    function send() {
      var text = input.value.trim();
      if (!text) return;
      input.disabled = true;
      onSend(text).then(function (ok) {
        var current = $('composer-input') || input;
        current.disabled = false;
        if (ok) { current.value = ''; S.drafts[key] = ''; }
        current.focus();
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
      else chat.appendChild(composer('conversation:' + c.id, function (text) {
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
        composer('party:' + p.id, function (text) {
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

  // ---- Games: what this console detects, and the tracked history ----------------------------
  // Artwork from the server when the game is verified there, else the icon this console has for
  // it (if installed), else the initials.
  function gameIcon(name, titleId, art, small) {
    var node = h('span', { className: 'game-icon' + (small ? ' tiny' : ''), 'aria-hidden': 'true', text: small ? '' : initials(name) });
    if (art && art.icon && /^[0-9a-f]{32}$/.test(art.icon)) fillImage(node, 'm:' + art.icon, mediaPath(art.icon));
    else if (titleId && /^[A-Z]{4}[0-9]{5}$/.test(titleId)) fillImage(node, 't:' + titleId, '/api/local-art?title=' + titleId);
    return node;
  }
  var ACTIVITY_TITLES = {
    playing: 'Now playing', excluded: 'Not shared', 'permission-missing': 'Permission needed', 'disabled-on-server': 'Turned off on the website',
    unsupported: 'Game detection unavailable', 'server-unsupported': 'Server update needed', idle: 'No game running', off: 'Game activity is off',
    starting: 'Starting', 'signed-out': 'Not paired'
  };
  function nowPlayingPanel() {
    var a = (S.app && S.app.activity) || { state: 'off', detail: '' };
    var panel = h('section', { className: 'panel', id: 'now-playing' }, h('h2', { text: 'On this console' }));
    if ((a.state === 'playing' || a.state === 'excluded') && (a.name || a.titleId)) {
      var seconds = a.startedAt ? secondsSince(a.startedAt) : a.sinceSeconds;
      panel.appendChild(h('div', { className: 'game-card' }, gameIcon(a.name || a.titleId, a.titleId, a.art),
        h('div', { className: 'grow' },
          h('div', { className: 'label', text: ACTIVITY_TITLES[a.state] }),
          h('div', { className: 'game-title', id: 'now-playing-title', text: a.name || a.titleId }),
          h('div', { className: 'sub', text: 'PlayStation 5 · ' + (a.state === 'playing' ? 'playing for ' : 'running for ') + durationText(seconds) + ' · ' + a.titleId }),
          h('div', { className: 'small muted', text: a.detail }))));
      panel.appendChild(h('p', { className: 'muted small', text: 'AKENO sees that this game is running on the console. It cannot tell whether the game is on screen.' }));
    } else {
      panel.appendChild(h('div', { className: 'name', id: 'now-playing-title', text: ACTIVITY_TITLES[a.state] || a.state }));
      panel.appendChild(h('p', { className: 'muted', text: a.detail || '' }));
      if (a.state === 'off' && S.app && S.app.paired) {
        panel.appendChild(h('button', { className: 'btn', id: 'activity-on', type: 'button', text: 'Share the game I am playing', onclick: function () {
          api('POST', '/api/settings', { activityEnabled: true }).then(function () { refreshState().then(render); });
        } }));
      }
    }
    return panel;
  }
  function historyPanel() {
    var r = S.myActivity;
    var panel = h('section', { className: 'panel' }, h('h2', { text: 'Recently played' }));
    if (!r) { panel.appendChild(h('p', { className: 'muted', text: 'Loading...' })); return panel; }
    if (!r.recentGames.length) {
      panel.appendChild(h('p', { className: 'muted', text: 'No games tracked yet. Games appear here when this console detects them.' }));
      return panel;
    }
    panel.appendChild(h('div', { className: 'list', id: 'recent-games' }, r.recentGames.map(function (g) {
      var detail = g.playingNow ? 'Playing now' : 'Last played ' + dayText(g.lastPlayedAt) +
        (g.lastSessionSeconds !== null ? ' · last session ' + durationText(g.lastSessionSeconds) : '');
      return h('div', { className: 'item', tabindex: '0', id: 'game-' + g.titleId }, gameIcon(gameName(g), g.titleId, g.art),
        h('span', { className: 'grow' },
          h('div', { className: 'name', text: gameName(g) + (g.excluded ? ' (excluded)' : '') }),
          h('div', { className: 'sub', text: detail + ' · ' + g.sessionCount + ' session' + (g.sessionCount === 1 ? '' : 's') })),
        h('span', { className: 'total' },
          h('div', { className: 'name', text: g.totalSeconds === null ? 'hidden' : durationText(g.totalSeconds) }),
          h('div', { className: 'sub', text: 'total tracked' })));
    })));
    if (r.trackedSince) {
      panel.appendChild(h('p', { className: 'muted small', text: 'Tracked by AKENO since ' + new Date(r.trackedSince).toLocaleDateString() +
        '. Earlier playtime is not known and is never estimated.' }));
    }
    return panel;
  }
  function viewGames() {
    return h('div', {}, nowPlayingPanel(), historyPanel(),
      h('p', { className: 'muted small', text: 'Who sees your game and history is set in the privacy settings on the AKENO NETWORK website.' }));
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
        standalone ? h('button', { className: 'btn ghost', type: 'button', id: 'diag-back', text: 'Back', onclick: function () {
          S.overlay = null;
          lastShape = '';
          refreshState();
        } }) : null),
      result));
    if (!d) {
      wrap.appendChild(h('p', { className: 'muted', text: 'Loading...' }));
      loadDiagnostics();
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
    if (d.activity) {
      var act = d.activity, scan = act.lastScan || {};
      wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Game detection' }), kv([
        ['State', act.state + (act.detail ? ' — ' + act.detail : '')], ['Method', act.method],
        ['Current', act.titleId ? act.titleId + (act.name ? ' (' + act.name + ', from ' + (act.nameSource || 'server') + ')' : '') : 'none'],
        ['Last scan', scan.result + ' · ' + scan.processes + ' processes, ' + scan.appProcesses + ' with a title ID, ' + scan.stoppedGames + ' stopped games' +
          (act.lastScanSecondsAgo >= 0 ? ' · ' + act.lastScanSecondsAgo + ' s ago' : '')],
        ['Candidates', (scan.candidates || []).join(', ') || 'none'],
        ['App info layout', scan.layout + ' (votes: offset 20 = ' + (scan.layoutVotes ? scan.layoutVotes.offset20 : 0) + ', offset 16 = ' +
          (scan.layoutVotes ? scan.layoutVotes.offset16 : 0) + ')'],
        ['Raw probes', (scan.probes || []).join('\n') || 'none'],
        ['Focus', act.focus],
        ['Heartbeats', act.heartbeatsOk + ' ok, ' + act.heartbeatsFailed + ' failed · every ' + Math.round(act.heartbeatIntervalMs / 1000) + ' s' +
          (act.lastHeartbeatSecondsAgo >= 0 ? ' · last ' + act.lastHeartbeatSecondsAgo + ' s ago' : '')],
        ['Errors', scan.error || 'none']])));
    }
    if (d.notifications.actionable) {
      var an = d.notifications.actionable;
      var testResult = h('p', { id: 'actionable-result', className: 'muted small', text: '' });
      wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Notifications with a button (experimental)' }), kv([
        ['Enabled', an.enabled ? 'yes' : 'no (Settings)'], ['Method', an.method], ['Status', an.status],
        ['Results', 'shown ' + an.sent + ', refused ' + an.refused + ', unavailable ' + an.unavailable + ' (a plain notification is sent instead)']]),
        h('div', { className: 'row mt' }, h('button', { className: 'btn secondary', type: 'button', id: 'diag-actionable-test', text: 'Send a test notification with a button',
          onclick: function (ev) {
            ev.target.disabled = true;
            api('POST', '/api/diagnostics/actionable-test').then(function (res) {
              ev.target.disabled = false;
              testResult.textContent = res.ok ? 'The system accepted it. Does it show a button, and does the button open AKENO? Please report what you see.'
                : 'Not shown (result ' + res.result + ': ' + res.status + '). Plain notifications keep working.';
            });
          } })), testResult));
    }
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Features' }),
      h('dl', { className: 'kv' }, d.capabilities.map(function (c) {
        return [h('dt', { text: c.feature }), h('dd', {}, h('span', { className: 'state-' + c.state, text: c.state }), ' — ' + c.reason)];
      }))));
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Recent log (tokens are redacted)' }),
      h('div', { className: 'log mono', text: d.log.join('\n') })));
    return wrap;
  }
  function loadDiagnostics() {
    api('GET', '/api/diagnostics').then(function (d) {
      if (!d.ok) return;
      S.diagnostics = d;
      if (S.overlay === 'diagnostics' || (S.app && S.app.paired && S.view === 'diagnostics')) render();
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
    var act = S.app.activity || {};
    return h('div', {},
      h('section', { className: 'panel' }, h('h2', { text: 'Notifications' }),
        h('p', { className: 'muted small', text: 'Shown as system notifications while AKENO NETWORK runs, also when this page is closed and while you play.' }),
        toggle('set-notify-messages', 'New messages', 'notifyMessages', n.messages),
        toggle('set-notify-social', 'Friend requests and invitations', 'notifySocial', n.social),
        toggle('set-notify-actionable', 'Experimental: message notifications with an "Open in AKENO" button', 'actionableNotifications', n.actionable),
        h('p', { className: 'muted small', text: 'Not yet verified on a console. If notifications stop appearing or the button does nothing, turn this off; ' +
          'AKENO then uses the plain notifications again. Starting AKENO again (or the newest-message banner) also opens the latest conversation.' })),
      h('section', { className: 'panel' }, h('h2', { text: 'Game activity' }),
        toggle('set-activity', 'Share the game I am playing from this console', 'activityEnabled', act.enabled),
        h('p', { className: 'muted small', text: 'AKENO looks for a running PS5 or PS4 game (read-only) and tells your AKENO server, which records the time ' +
          'you play. Who sees it is set in the privacy settings on the website, where you can also exclude games or turn tracking off.' })),
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
        if (S.view === 'friends' && !isTyping()) render();
        break;
      case 'activity.local':
        if (S.app) S.app.activity = d;
        if (S.view === 'games' && !isTyping()) render();
        break;
      case 'activity.self':
        loadActivity();
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
    if (S.overlay && S.app && S.app.configured) { S.overlay = null; S.setupDraft = null; lastShape = ''; render(); focusFirst(); return; }
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
    } else if (ev.key === 'Escape' || (ev.key === 'Backspace' && !typing)) {
      // Escape (Circle on many controllers) also leaves a text field; Backspace keeps deleting text.
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
      if (S.view === 'games') loadActivity();
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

  // A second launch of AKENO opens the same page with a new fragment: only "hashchange" fires.
  window.addEventListener('hashchange', function () {
    var previous = token;
    readFragment();
    if (appGone && token && token !== previous) { window.location.reload(); return; }
    if (pendingOpen && S.app && S.app.paired) applyOpen();
    else onReturn();
  });
  document.addEventListener('visibilitychange', function () { if (!document.hidden) onReturn(); });
  window.addEventListener('focus', onReturn);

  if (!token) {
    showExpired();
  } else {
    refreshState(); // Loads everything (and applies an "open" target) once the console is paired.
    setInterval(function () { if (!appGone) refreshState(); }, 3000);
    setInterval(function () { if (!appGone) pollEvents(); }, 2000);
    // Running durations ("Playing ... · 47 min") stay current without disturbing typing.
    setInterval(function () {
      if (!appGone && S.app && S.app.paired && (S.view === 'friends' || S.view === 'games') && !isTyping() && !S.overlay) render();
    }, 30000);
  }
})();
