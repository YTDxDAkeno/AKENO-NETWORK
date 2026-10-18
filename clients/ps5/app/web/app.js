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
  var UUID = '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}';
  var UUID_PATTERN = new RegExp('^' + UUID + '$');
  var OPEN_PATTERN = new RegExp('^(messages|party|friends|games|invitations|profile|conversation:' + UUID + '|profile:' + UUID + ')$');
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

  // ---- Page memory -------------------------------------------------------------------------
  // The last screen and unsent drafts survive a reload of this page (the system browser may
  // reload it after a game). Kept in this tab's sessionStorage only; never sent anywhere.
  var VIEW_KEY = 'akeno.view';
  var DRAFTS_KEY = 'akeno.drafts';
  function remember(key, value) {
    try { window.sessionStorage.setItem(key, JSON.stringify(value)); } catch (e) { /* unavailable or full */ }
  }
  function recall(key) {
    try { var raw = window.sessionStorage.getItem(key); return raw ? JSON.parse(raw) : null; } catch (e) { return null; }
  }
  function recallDrafts() {
    var saved = recall(DRAFTS_KEY), drafts = {}, count = 0;
    if (saved && typeof saved === 'object') {
      Object.keys(saved).forEach(function (key) {
        if (count < 20 && /^(conversation|party):[0-9a-f-]{36}$/.test(key) && typeof saved[key] === 'string' && saved[key]) {
          drafts[key] = saved[key].slice(0, 2000);
          count++;
        }
      });
    }
    return drafts;
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
        if (!data && /^text\/plain/.test(xhr.getResponseHeader('Content-Type') || '')) data = { ok: xhr.status === 200, text: xhr.responseText };
        if (!data) data = { ok: false, error: { code: 'BAD_RESPONSE', message: 'Unexpected response from the console app.' } };
        data.httpStatus = xhr.status;
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
  // Profile pictures, banners and game artwork come from the AKENO server through the console
  // app; icons of games installed on this console are read from its own metadata (read-only,
  // never sent anywhere). Both arrive as small JSON documents and are shown as data: URLs, so the
  // page keeps its strict policy (images only from itself or data:) and the device credential
  // never reaches the page. The cache is bounded by count and by size (the console browser has
  // little memory); a broken or undecodable image simply keeps the initials or the theme banner.
  var IMAGE_CACHE = {};
  var IMAGE_WAITERS = {};
  var IMAGE_ORDER = [];
  var IMAGE_BYTES = 0;
  var IMAGE_CACHE_MAX = 80;
  var IMAGE_BYTES_MAX = 8 * 1024 * 1024; // characters of data: URLs (about 6 MB of images)
  var MEDIA_ID = /^[0-9a-f]{32}$/;
  function dropImage(key) {
    var at = IMAGE_ORDER.indexOf(key);
    if (at >= 0) IMAGE_ORDER.splice(at, 1);
    if (key in IMAGE_CACHE) IMAGE_BYTES -= IMAGE_CACHE[key] ? IMAGE_CACHE[key].length : 0;
    delete IMAGE_CACHE[key];
  }
  function cacheImage(key, url) {
    dropImage(key);
    IMAGE_CACHE[key] = url;
    IMAGE_ORDER.push(key);
    IMAGE_BYTES += url ? url.length : 0;
    while (IMAGE_ORDER.length > 1 && (IMAGE_ORDER.length > IMAGE_CACHE_MAX || IMAGE_BYTES > IMAGE_BYTES_MAX)) dropImage(IMAGE_ORDER[0]);
  }
  function loadImage(key, path, done) {
    if (key in IMAGE_CACHE) {
      var hit = IMAGE_CACHE[key];
      IMAGE_ORDER.splice(IMAGE_ORDER.indexOf(key), 1);
      IMAGE_ORDER.push(key); // most recently used
      done(hit);
      return;
    }
    if (IMAGE_WAITERS[key]) { IMAGE_WAITERS[key].push(done); return; }
    IMAGE_WAITERS[key] = [done];
    api('GET', path).then(function (res) {
      var ok = res.ok && /^image\/(png|jpeg|webp)$/.test(res.type || '') && /^[A-Za-z0-9+/=]+$/.test(res.data || '');
      var url = ok ? 'data:' + res.type + ';base64,' + res.data : false;
      // Missing images are remembered (no retry storm); network and pairing failures are not.
      if (ok || res.httpStatus === 404 || res.httpStatus === 400) cacheImage(key, url);
      var waiters = IMAGE_WAITERS[key] || [];
      delete IMAGE_WAITERS[key];
      waiters.forEach(function (cb) { cb(url); });
    });
  }
  // Shows the image inside holder (replacing its initials); never moves the focus.
  function fillImage(holder, key, path) {
    loadImage(key, path, function (url) {
      if (!url || !holder) return;
      var fallback = holder.textContent;
      var img = h('img', { src: url, alt: '', draggable: 'false' });
      img.onerror = function () { // e.g. a format this browser cannot decode
        if (img.parentNode) img.parentNode.removeChild(img);
        holder.className = holder.className.replace(/ has-image/g, '');
        if (!holder.firstChild) holder.textContent = fallback;
        cacheImage(key, false);
      };
      holder.textContent = '';
      holder.insertBefore(img, holder.firstChild);
      holder.className += ' has-image';
    });
  }
  // WebP support decides between the large banner variant and the JPEG fallback.
  var webpSupported = false;
  (function () {
    var probe = new Image();
    probe.onload = function () { webpSupported = probe.width > 0; };
    probe.src = 'data:image/webp;base64,UklGRhoAAABXRUJQVlA4TA0AAAAvAAAAEAcQERGIiP4HAA==';
  })();
  function mediaPath(id, variant) { return '/api/media?id=' + id + '&v=' + (variant || 'fallback'); }
  function fillBanner(holder, mediaId) {
    var variant = webpSupported ? 'lg' : 'fallback';
    loadImage('b:' + variant + ':' + mediaId, mediaPath(mediaId, variant), function (url) {
      if (url) { showBanner(holder, url); return; }
      if (variant === 'lg') loadImage('b:fallback:' + mediaId, mediaPath(mediaId), function (jpeg) { if (jpeg) showBanner(holder, jpeg); });
    });
  }
  function showBanner(holder, url) {
    var img = h('img', { className: 'banner-img', src: url, alt: '', draggable: 'false' });
    img.onerror = function () { if (img.parentNode) img.parentNode.removeChild(img); };
    holder.insertBefore(img, holder.firstChild);
    holder.className += ' has-image';
  }
  function avatarNode(user, presence, big) {
    var node = h('span', { className: 'avatar' + (big ? ' big' : ''), text: initials(user.displayName || user.onlineId) });
    if (presence) node.appendChild(h('span', { className: 'dot ' + presence }));
    if (user.avatarImage && MEDIA_ID.test(user.avatarImage)) {
      var variant = big && webpSupported ? 'md' : 'fallback';
      fillImage(node, 'm:' + user.avatarImage + (variant === 'md' ? ':md' : ''), mediaPath(user.avatarImage, variant));
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
    drafts: recallDrafts(), myActivity: null, bannerDismissed: 0,
    // Profile on screen: { id, data, activity, error, activityError, loading }. Never cached:
    // every open asks the server again, which applies the owner's privacy settings and blocks.
    profile: null, profileReturn: null, profileStack: []
  };
  function saveDrafts() { remember(DRAFTS_KEY, S.drafts); }
  // Audio lab builds serve /audiolab.js (docs/PS5_AUDIO_RESEARCH.md); the release serves an empty script.
  var audioLab = window.AkenoAudioLab ? window.AkenoAudioLab({ h: h, $: $, api: api, kv: kv, toast: toast }) : null;

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
      var badge = $('release-badge');
      if (badge && st.app && st.app.release) badge.textContent = st.app.release + (st.app.audioLab ? ' · Audio lab v2' : '');
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
      } else if (st.paired && S.view === 'settings' && before && quickKey(before.quickAccess) !== quickKey(st.quickAccess) && !isTyping()) {
        render();
      }
      renderBanner();
    });
  }
  function activityKey(a) { return a ? [a.enabled, a.state, a.titleId, a.name, a.startedAt].join('|') : ''; }
  function quickKey(q) {
    return q ? [q.enabled, q.state, q.combo, q.holdMs, q.detections, q.activations, q.test && q.test.active, q.test && q.test.secondsLeft,
      q.test && q.test.detections].join('|') : '';
  }

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
    if (target === 'profile' || target.indexOf('profile:') === 0) {
      S.profile = null;
      openProfile(target === 'profile' ? null : target.slice(8), { view: 'friends' });
      return;
    }
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
    if (!target) {
      // Nothing asked for: come back to the screen this page showed before it was reloaded.
      var last = recall(VIEW_KEY);
      if (last && typeof last.target === 'string') {
        if (OPEN_PATTERN.test(last.target)) openTarget(last.target);
        else if (/^(sessions|diagnostics|settings)$/.test(last.target)) { openView(last.target); focusFirst(); }
      }
      return;
    }
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

  // SharePlay (experimental) lives in shareplay.js; see docs/SHAREPLAY_INTEGRATION.md.
  function viewSharePlay() {
    if (!window.AkenoSharePlay) return h('section', { className: 'panel' }, h('p', { text: 'SharePlay is not available in this build.' }));
    return window.AkenoSharePlay.view({ h: h, api: api, server: server, S: S });
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
      // A friend's profile belongs to the screen it was opened from; only my own profile is "Profile".
      var tabView = S.view;
      if (S.view === 'profile' && S.profile && S.me && S.profile.id !== S.me.id) tabView = (S.profileReturn && S.profileReturn.view) || 'friends';
      Array.prototype.forEach.call(document.querySelectorAll('.tab'), function (t) {
        t.classList.toggle('active', t.getAttribute('data-view') === tabView);
      });
      var views = { friends: viewFriends, messages: viewMessages, profile: viewProfile, games: viewGames, invitations: viewInvitations,
        party: viewParty, sessions: viewSessions, diagnostics: viewDiagnostics, settings: viewSettings, shareplay: viewSharePlay };
      main.appendChild((views[S.view] || viewFriends)());
      remember(VIEW_KEY, { target: currentTarget() });
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

  // What render() shows, as an "open" target (restored after a reload of the page).
  function currentTarget() {
    if (S.view === 'profile') return S.profile && S.profile.id && !(S.me && S.profile.id === S.me.id) ? 'profile:' + S.profile.id : 'profile';
    if (S.view === 'messages' && S.openConversation) return 'conversation:' + S.openConversation.id;
    return S.view;
  }
  // Switches to a top-level view (tab), loading what it shows.
  function openView(view) {
    S.overlay = null;
    if (view === 'profile') { S.profile = null; openProfile(null, { view: 'friends' }); return; }
    S.view = view;
    if (view === 'diagnostics') S.diagnostics = null;
    var loaders = { friends: loadFriends, messages: loadConversations, invitations: loadInvitations, party: loadParty, sessions: loadSessions,
      games: loadActivity };
    if (loaders[view]) loaders[view]();
    render();
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
            h('button', { className: 'btn secondary', type: 'button', id: 'req-profile-' + r.id, text: 'Profile', onclick: function () { openProfile(r.user.id); } }),
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
        // Selecting a friend opens their profile; Message is right next to it.
        return h('div', { className: 'friend-row' },
          personRow(f, h('span', { className: 'row-hint', text: 'View profile' }), function () { openProfile(f.id); }, 'friend-' + f.id),
          h('button', { className: 'btn secondary friend-msg', type: 'button', id: 'friend-msg-' + f.id, text: 'Message', 'aria-label': 'Message ' + (f.displayName || f.onlineId),
            onclick: function () { openChatWith(f); } }));
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
    input.addEventListener('input', function () { S.drafts[key] = input.value; saveDrafts(); });
    function send() {
      var text = input.value.trim();
      if (!text) return;
      input.disabled = true;
      onSend(text).then(function (ok) {
        var current = $('composer-input') || input;
        current.disabled = false;
        if (ok) { current.value = ''; delete S.drafts[key]; saveDrafts(); }
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
      chat.appendChild(h('div', { className: 'row chat-head' }, h('h2', { text: c.otherUser.displayName || c.otherUser.onlineId }), h('span', { className: 'spacer' }),
        c.otherUser.id && !c.otherUser.deleted ? h('button', { className: 'btn ghost', type: 'button', id: 'conv-profile', text: 'View profile',
          onclick: function () { openProfile(c.otherUser.id); } }) : null));
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
          return personRow(m.user, m.isLeader ? h('span', { className: 'badge', text: 'Leader' }) : null,
            m.user && m.user.id && !m.user.deleted ? function () { openProfile(m.user.id); } : null, 'party-member-' + (m.user && m.user.id));
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

  // ---- Profiles -------------------------------------------------------------------------------
  // My profile and friends' profiles, as the AKENO server returns them for this account
  // (GET /users/:id and /users/:id/activity through the console app, "profile" permission). The
  // server applies every privacy rule: private and friends-only profiles, invisible presence,
  // hidden game activity, history and playtime, excluded games and blocks. Nothing is inferred
  // from local data and nothing is kept between opens.
  var BADGES = {
    founder: { name: 'AKENO Founder', color: '#e9b949', description: 'Founded this AKENO NETWORK community.' },
    administrator: { name: 'Administrator', color: '#ef5b5b', description: 'Runs and moderates this AKENO NETWORK server.' },
    developer: { name: 'Developer', color: '#38bdf8', description: 'Builds AKENO NETWORK.' },
    verified_contributor: { name: 'Verified Contributor', color: '#34d399', description: 'Contribution confirmed by the administrators.' },
    early_supporter: { name: 'Early Supporter', color: '#f472b6', description: 'Supported this community from the start.' },
    beta_tester: { name: 'Beta Tester', color: '#a78bfa', description: 'Tested AKENO NETWORK before release.' },
    ps5_alpha_tester: { name: 'PS5 Alpha Tester', color: '#5b8cff', description: 'Tested the AKENO console app on real PS5 hardware.' },
    event_participant: { name: 'Community Event Participant', color: '#fb923c', description: 'Took part in an AKENO community event.' }
  };
  // Line icons of the website's badge medallions: path data of the Lucide icons crown,
  // shield-check, braces, badge-check, sprout, flask-conical, gamepad-2 and ticket (lucide-react
  // 1.56.0), unmodified. None of them is derived from Feather, so only this licence applies:
  //
  //   ISC License
  //   Copyright (c) 2026 Lucide Icons and Contributors
  //   Permission to use, copy, modify, and/or distribute this software for any purpose with or
  //   without fee is hereby granted, provided that the above copyright notice and this permission
  //   notice appear in all copies.
  //   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
  //   SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
  //   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
  //   DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
  //   CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
  //   OR PERFORMANCE OF THIS SOFTWARE.
  //
  // See docs/LICENSES.md. The hexagon medallion around each icon is AKENO's own artwork.
  var BADGE_ICONS = {
    founder: [['path',{'d':'M11.562 3.266a.5.5 0 0 1 .876 0L15.39 8.87a1 1 0 0 0 1.516.294L21.183 5.5a.5.5 0 0 1 .798.519l-2.834 10.246a1 1 0 0 1-.956.734H5.81a1 1 0 0 1-.957-.734L2.02 6.02a.5.5 0 0 1 .798-.519l4.276 3.664a1 1 0 0 0 1.516-.294z'}],['path',{'d':'M5 21h14'}]],
    administrator: [['path',{'d':'M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z'}],['path',{'d':'m9 12 2 2 4-4'}]],
    developer: [['path',{'d':'M8 3H7a2 2 0 0 0-2 2v5a2 2 0 0 1-2 2 2 2 0 0 1 2 2v5c0 1.1.9 2 2 2h1'}],['path',{'d':'M16 21h1a2 2 0 0 0 2-2v-5c0-1.1.9-2 2-2a2 2 0 0 1-2-2V5a2 2 0 0 0-2-2h-1'}]],
    verified_contributor: [['path',{'d':'M3.85 8.62a4 4 0 0 1 4.78-4.77 4 4 0 0 1 6.74 0 4 4 0 0 1 4.78 4.78 4 4 0 0 1 0 6.74 4 4 0 0 1-4.77 4.78 4 4 0 0 1-6.75 0 4 4 0 0 1-4.78-4.77 4 4 0 0 1 0-6.76Z'}],['path',{'d':'m16 9-5.5 5.5L8 12'}]],
    early_supporter: [['path',{'d':'M14 9.536V7a4 4 0 0 1 4-4h1.5a.5.5 0 0 1 .5.5V5a4 4 0 0 1-4 4 4 4 0 0 0-4 4c0 2 1 3 1 5a5 5 0 0 1-1 3'}],['path',{'d':'M4 9a5 5 0 0 1 8 4 5 5 0 0 1-8-4'}],['path',{'d':'M5 21h14'}]],
    beta_tester: [['path',{'d':'M14 2v6a2 2 0 0 0 .245.96l5.51 10.08A2 2 0 0 1 18 22H6a2 2 0 0 1-1.755-2.96l5.51-10.08A2 2 0 0 0 10 8V2'}],['path',{'d':'M6.453 15h11.094'}],['path',{'d':'M8.5 2h7'}]],
    ps5_alpha_tester: [['line',{'x1':'6','x2':'10','y1':'11','y2':'11'}],['line',{'x1':'8','x2':'8','y1':'9','y2':'13'}],['line',{'x1':'15','x2':'15.01','y1':'12','y2':'12'}],['line',{'x1':'18','x2':'18.01','y1':'10','y2':'10'}],['path',{'d':'M17.32 5H6.68a4 4 0 0 0-3.978 3.59c-.006.052-.01.101-.017.152C2.604 9.416 2 14.456 2 16a3 3 0 0 0 3 3c1 0 1.5-.5 2-1l1.414-1.414A2 2 0 0 1 9.828 16h4.344a2 2 0 0 1 1.414.586L17 18c.5.5 1 1 2 1a3 3 0 0 0 3-3c0-1.545-.604-6.584-.685-7.258-.007-.05-.011-.1-.017-.151A4 4 0 0 0 17.32 5z'}]],
    event_participant: [['path',{'d':'M2 9a3 3 0 0 1 0 6v2a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-2a3 3 0 0 1 0-6V7a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2Z'}],['path',{'d':'M13 5v2'}],['path',{'d':'M13 17v2'}],['path',{'d':'M13 11v2'}]]
  };
  var THEMES = {
    'default': { accent: '#4f7cff', from: '#1b2a55', to: '#2a1f4d' }, purple: { accent: '#8b5cf6', from: '#2b1854', to: '#140d2b' },
    blue: { accent: '#2f7bff', from: '#0b2f6b', to: '#081a3a' }, crimson: { accent: '#ef4452', from: '#4f1220', to: '#1f0a10' },
    midnight: { accent: '#c3cbe0', from: '#161a24', to: '#05070b' }, sakura: { accent: '#f472b6', from: '#4a1838', to: '#1f0c1a' },
    emerald: { accent: '#10b981', from: '#0b3b33', to: '#07191a' }, custom: { accent: '#4f7cff', from: '#1b2a55', to: '#2a1f4d' }
  };
  var SVG_NS = 'http://www.w3.org/2000/svg';

  function hexRgb(hex) { return [parseInt(hex.slice(1, 3), 16), parseInt(hex.slice(3, 5), 16), parseInt(hex.slice(5, 7), 16)]; }
  function luminance(rgb) {
    var c = rgb.map(function (v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); });
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  }
  // The profile's accent, lightened until it reads on this page's dark surface (4.5:1, as on the website).
  function readableAccent(hex) {
    var rgb = /^#[0-9a-f]{6}$/i.test(hex || '') ? hexRgb(hex.toLowerCase()) : hexRgb('#4f7cff');
    var surface = luminance(hexRgb('#111829'));
    for (var i = 0; i < 20 && (luminance(rgb) + 0.05) / (surface + 0.05) < 4.5; i++) {
      rgb = rgb.map(function (v) { return Math.round(v + (255 - v) * 0.12); });
    }
    return '#' + rgb.map(function (v) { return (v + 256).toString(16).slice(1); }).join('');
  }
  function themeOf(appearance) {
    var theme = THEMES[appearance && appearance.theme] || THEMES['default'];
    var accent = appearance && appearance.theme === 'custom' && appearance.accentColor ? appearance.accentColor : theme.accent;
    return { accent: readableAccent(accent), from: theme.from, to: theme.to };
  }

  function svg(tag, attrs) {
    var node = document.createElementNS(SVG_NS, tag);
    Object.keys(attrs).forEach(function (k) { node.setAttribute(k, attrs[k]); });
    return node;
  }
  // Original AKENO medallion (same construction as the website): hexagon in the badge colour with a line icon.
  function badgeMedal(id, size) {
    var def = BADGES[id];
    var box = h('span', { className: 'medal ' + (size || 'md'), title: def ? def.name + ' — ' + def.description : id });
    var mark = svg('svg', { viewBox: '0 0 32 32', 'aria-hidden': 'true', focusable: 'false' });
    mark.appendChild(svg('path', { d: 'M16 1.8 28.3 8.9v14.2L16 30.2 3.7 23.1V8.9Z', fill: def ? def.color : '#5b6785' }));
    mark.appendChild(svg('path', { d: 'M16 1.8 28.3 8.9 16 16Z', fill: '#fff', opacity: '0.14' }));
    mark.appendChild(svg('path', { d: 'M16 1.8 28.3 8.9v14.2L16 30.2 3.7 23.1V8.9Z', fill: 'none', stroke: '#fff', 'stroke-opacity': '0.35', 'stroke-width': '1.2' }));
    var icon = svg('g', { transform: 'translate(8.8 8.8) scale(0.6)', fill: 'none', stroke: '#0b0f1a', 'stroke-width': '2.6', 'stroke-linecap': 'round',
      'stroke-linejoin': 'round' });
    (BADGE_ICONS[id] || []).forEach(function (part) { icon.appendChild(svg(part[0], part[1])); });
    mark.appendChild(icon);
    box.appendChild(mark);
    return box;
  }
  function badgeChip(id) {
    var def = BADGES[id];
    if (!def) return null;
    return h('span', { className: 'badge-chip', title: def.description }, badgeMedal(id, 'sm'), h('span', { text: def.name }));
  }

  function relationshipLabel(rel) {
    return { self: 'You', friend: 'Friends', request_sent: 'Friend request sent', request_received: 'Wants to be your friend', blocked: 'Blocked',
      none: 'Not friends' }[rel.state] || '';
  }
  function presenceSentence(p) {
    if (!p) return 'Offline';
    if (p.game) return 'Playing ' + gameName(p.game) + ' · ' + durationText(secondsSince(p.game.startedAt));
    if (p.status === 'offline') return p.lastSeenAt ? 'Last seen ' + dayText(p.lastSeenAt) + ' ' + timeText(p.lastSeenAt) : 'Offline';
    return presenceLabel(p.status);
  }

  var profileSeq = 0;
  // Opens a profile (null = my own). The way back (Circle) returns to where it was opened from.
  function openProfile(userId, ret) {
    var id = userId || (S.me && S.me.id);
    if (!id) { loadMe().then(function () { if (S.me) openProfile(null, ret); }); return; }
    if (!UUID_PATTERN.test(id)) return;
    var focused = document.activeElement && document.activeElement.id;
    if (S.view !== 'profile' || !S.profile) {
      S.profileReturn = ret || { view: S.view, focus: focused, conversation: S.openConversation };
      S.profileStack = [];
    } else if (S.profile.id && S.profile.id !== id) {
      S.profileStack.push({ id: S.profile.id, focus: focused });
      if (S.profileStack.length > 10) S.profileStack.shift();
    }
    S.view = 'profile';
    S.overlay = null;
    loadProfile(id);
  }
  function profileError(res) {
    var code = res.error && res.error.code;
    if (res.offline) return { kind: 'app', title: 'The AKENO console app is not responding', text: 'Start AKENO NETWORK again on your console.' };
    if (code === 'UNPAIRED' || code === 'NOT_PAIRED') return { kind: 'unpaired', title: 'This console is not paired', text: 'Pair it again to see profiles.' };
    if (res.status === 404 || code === 'NOT_FOUND') {
      return { kind: 'missing', title: 'Profile not available', text: 'This profile does not exist or is not available to you.' };
    }
    if (res.status === 403) {
      return { kind: 'forbidden', title: 'Not allowed', text: (res.error && res.error.message) || 'This console may not read profiles.' };
    }
    return { kind: 'network', title: 'Profile could not be loaded', text: (res.error && res.error.message) || 'The AKENO server could not be reached.' };
  }
  function loadProfile(id) {
    var seq = ++profileSeq;
    S.profile = { id: id, data: null, activity: null, error: null, activityError: null, loading: true };
    render();
    if (!document.activeElement || document.activeElement === document.body || !$('main').contains(document.activeElement)) focusFirst();
    var base = '/api/v1/users/' + id;
    return Promise.all([api('GET', base), api('GET', base + '/activity')]).then(function (r) {
      if (seq !== profileSeq || !S.profile || S.profile.id !== id) return; // another profile was opened meanwhile
      var p = r[0], a = r[1];
      S.profile.loading = false;
      if (p.ok) S.profile.data = p.data; else S.profile.error = profileError(p);
      if (a.ok) S.profile.activity = a.data; else S.profile.activityError = profileError(a);
      if (!p.ok && (S.profile.error.kind === 'unpaired')) refreshState();
      if (S.view !== 'profile') return;
      var focused = document.activeElement && document.activeElement.id;
      render();
      // Fresh profile: start on its first action (Message, Accept, ...) instead of the loading screen's Back.
      if (!focused || focused === 'profile-back' || !$(focused)) {
        var first = document.querySelector('#profile-actions button') || $('profile-back');
        if (first) first.focus();
      }
    });
  }
  // Circle / Back: the previous profile, else the screen the first profile was opened from.
  function closeProfile() {
    var previous = S.profileStack.pop();
    if (previous) {
      loadProfile(previous.id).then(function () { var el = previous.focus && $(previous.focus); if (el) el.focus(); });
      return;
    }
    var ret = S.profileReturn || { view: 'friends' };
    S.profileReturn = null;
    S.profile = null;
    profileSeq++;
    S.view = ret.view && ret.view !== 'profile' ? ret.view : 'friends';
    if (S.view === 'messages' && ret.conversation) S.openConversation = ret.conversation;
    render();
    var el = ret.focus && $(ret.focus);
    if (el) el.focus(); else focusFirst();
  }

  function profileAction(id, label, onclick, kind) {
    return h('button', { className: 'btn' + (kind ? ' ' + kind : ''), type: 'button', id: id, text: label, onclick: onclick });
  }
  function busy(ev, promise) {
    var button = ev && ev.target;
    if (button) button.disabled = true;
    return promise.then(function (result) { if (button) button.disabled = false; return result; });
  }
  function profileActions(d) {
    var u = d.user, rel = d.relationship;
    var list = [];
    if (rel.state === 'request_received' && rel.friendRequestId) {
      list.push(profileAction('profile-accept', 'Accept friend request', function (ev) {
        busy(ev, server('POST', '/friends/requests/' + rel.friendRequestId + '/accept')).then(function (ok) {
          if (ok) { toast('You are now friends with ' + (u.displayName || u.onlineId) + '.'); loadFriends(); loadProfile(u.id); }
        });
      }));
      list.push(profileAction('profile-decline', 'Decline', function (ev) {
        busy(ev, server('POST', '/friends/requests/' + rel.friendRequestId + '/decline')).then(function (ok) { if (ok) { loadFriends(); loadProfile(u.id); } });
      }, 'secondary'));
    }
    if (rel.state === 'none' && rel.canSendFriendRequest) {
      list.push(profileAction('profile-add', 'Add friend', function (ev) {
        busy(ev, server('POST', '/friends/requests', { userId: u.id })).then(function (ok) { if (ok) { toast('Friend request sent.'); loadProfile(u.id); } });
      }));
    }
    if (rel.state === 'request_sent' && rel.friendRequestId) {
      list.push(profileAction('profile-cancel-request', 'Cancel friend request', function (ev) {
        busy(ev, server('POST', '/friends/requests/' + rel.friendRequestId + '/cancel')).then(function (ok) { if (ok) loadProfile(u.id); });
      }, 'secondary'));
    }
    if (rel.state !== 'self' && rel.canMessage) {
      list.push(profileAction('profile-message', 'Message', function () { openChatWith(u); }, list.length ? 'secondary' : ''));
    }
    var myParty = S.party && S.party.isMember ? S.party : null;
    var inMyParty = myParty && (myParty.members || []).some(function (m) { return m.user && m.user.id === u.id; });
    if (rel.state === 'friend' && myParty && !inMyParty) {
      list.push(profileAction('profile-invite', 'Invite to party', function (ev) {
        busy(ev, server('POST', '/parties/' + myParty.id + '/invitations', { userId: u.id })).then(function (ok) { if (ok) toast('Party invitation sent.'); });
      }, 'secondary'));
    }
    if (rel.state === 'self') {
      list.push(profileAction('profile-games', 'My games', function () { openView('games'); focusFirst(); }, 'secondary'));
    }
    list.push(profileAction('profile-back', 'Back', closeProfile, 'ghost'));
    return h('div', { className: 'profile-actions', id: 'profile-actions' }, list);
  }

  // Game artwork: cover (2:3), banner (16:9) or icon from the server, else this console's icon, else initials.
  function gameArtNode(game, shape, id) {
    var name = gameName(game);
    var node = h('span', { className: 'art ' + shape, 'aria-hidden': 'true', text: initials(name) });
    var art = game && game.art;
    var media = art && (shape === 'cover' ? art.cover || art.icon : shape === 'wide' ? art.banner || art.cover || art.icon : art.icon || art.cover);
    if (media && MEDIA_ID.test(media)) fillImage(node, 'm:' + media, mediaPath(media));
    else if (game && game.titleId && /^[A-Z]{4}[0-9]{5}$/.test(game.titleId)) fillImage(node, 't:' + game.titleId, '/api/local-art?title=' + game.titleId);
    if (id) node.id = id;
    return node;
  }
  function gameCard(game, id, lines) {
    return h('div', { className: 'game-tile', tabindex: '0', id: id, 'aria-label': gameName(game) },
      gameArtNode(game, 'cover'),
      h('div', { className: 'tile-name', text: gameName(game) }),
      lines.filter(Boolean).map(function (line) { return h('div', { className: 'tile-sub', text: line }); }));
  }
  function profileSection(title, id, body, extra) {
    return h('section', { className: 'panel pcard', id: id }, h('h2', { className: 'pcard-title', text: title }), extra || null, body);
  }

  function nowPlayingCard(game) {
    return h('div', { className: 'now-card', tabindex: '0', id: 'profile-now' },
      gameArtNode(game, 'wide'),
      h('div', { className: 'now-info' },
        h('div', { className: 'label', text: 'Now playing' }),
        h('div', { className: 'game-title', text: gameName(game) }),
        h('div', { className: 'now-sub', text: (game.platform || 'PS5') + ' · playing for ' + durationText(secondsSince(game.startedAt)) +
          (game.titleId ? ' · ' + game.titleId : '') })));
  }

  function recentGamesSection(P, isSelf) {
    var a = P.activity;
    var body;
    if (!a) body = h('p', { className: 'muted', text: P.activityError ? 'Game activity could not be loaded (' + P.activityError.text + ')' : 'Loading...' });
    else if (!a.historyVisible) body = h('p', { className: 'muted', text: 'This player does not share their game history with you.' });
    else if (!a.recentGames.length) body = h('p', { className: 'muted', text: isSelf ? 'No games tracked yet. Games appear here when this console detects them.' : 'No games played recently.' });
    else {
      body = h('div', { className: 'tile-row', id: 'profile-recent' }, a.recentGames.slice(0, 12).map(function (g) {
        return gameCard(g, 'pg-recent-' + g.titleId, [
          g.playingNow ? 'Playing now' : 'Last played ' + dayText(g.lastPlayedAt),
          g.totalSeconds !== null && g.totalSeconds !== undefined ? durationText(g.totalSeconds) + ' tracked' : null,
          g.excluded ? 'Excluded (only you see it)' : null
        ]);
      }));
    }
    return profileSection('Recently played', 'profile-recent-section', body);
  }

  function statsSection(a) {
    if (!a || !a.historyVisible || !a.totals || !a.totals.sessions) return null;
    var rows = [['Tracked playtime', a.totals.seconds === null ? 'Hidden' : a.totals.seconds < 60 ? '< 1 min' : durationText(a.totals.seconds)],
      ['Games played', String(a.totals.games)],
      ['Sessions', String(a.totals.sessions)]];
    if (a.trackedSince) rows.push(['Tracked since', new Date(a.trackedSince).toLocaleDateString()]);
    return h('section', { className: 'panel pcard', id: 'profile-stats', tabindex: '0' }, h('h2', { className: 'pcard-title', text: 'Gaming activity' }),
      h('dl', { className: 'stats' }, rows.map(function (r) { return h('div', { className: 'stat' }, h('dt', { text: r[0] }), h('dd', { text: r[1] })); })),
      h('p', { className: 'muted small', text: 'Time tracked by AKENO from confirmed console activity only.' }));
  }

  function partySection(d, isSelf) {
    var party = isSelf ? (S.party && S.party.isMember ? { id: S.party.id, name: S.party.name, memberCount: S.party.memberCount,
      maxMembers: S.party.maxMembers, isMember: true } : null) : d.party;
    if (!party) return null;
    var full = party.memberCount >= party.maxMembers;
    var action = party.isMember ? h('button', { className: 'btn secondary', type: 'button', id: 'profile-party-open', text: 'Open party',
      onclick: function () { openView('party'); focusFirst(); } })
      : full ? h('p', { className: 'muted small', text: 'This party is full.' })
        : h('button', { className: 'btn', type: 'button', id: 'profile-party-join', text: 'Join party', onclick: function (ev) {
          busy(ev, server('POST', '/parties/' + party.id + '/join')).then(function (ok) { if (ok) { toast('Joined the party.'); loadParty(); loadProfile(d.user.id); } });
        } });
    return h('section', { className: 'panel pcard', id: 'profile-party' }, h('h2', { className: 'pcard-title', text: 'Party' }),
      h('div', { className: 'name', text: party.name || 'Party' }),
      h('p', { className: 'muted small', text: party.memberCount + ' of ' + party.maxMembers + ' members' + (party.isMember ? ' · You are in this party' : '') }),
      action);
  }

  function viewProfile() {
    var P = S.profile;
    var wrap = h('div', { className: 'profile-page', id: 'profile-page' });
    if (!P || (P.loading && !P.data)) {
      wrap.appendChild(h('section', { className: 'profile-hero loading' }, h('div', { className: 'profile-banner' }),
        h('div', { className: 'profile-head' }, h('span', { className: 'avatar big placeholder' }),
          h('div', { className: 'profile-ident' }, h('h1', { className: 'profile-name', text: 'Loading profile...' })),
          h('div', { className: 'profile-actions', id: 'profile-actions' }, profileAction('profile-back', 'Back', closeProfile, 'ghost')))));
      return wrap;
    }
    if (!P.data) {
      var e = P.error || { title: 'Profile not available', text: '' };
      wrap.appendChild(h('section', { className: 'panel center', id: 'profile-error' }, h('h1', { text: e.title }), h('p', { className: 'muted', text: e.text }),
        h('div', { className: 'row centered mt' },
          e.kind === 'network' || e.kind === 'app' ? profileAction('profile-retry', 'Retry', function () { loadProfile(P.id); }) : null,
          profileAction('profile-back', 'Back', closeProfile, 'ghost'))));
      return wrap;
    }
    var d = P.data, u = d.user, rel = d.relationship, isSelf = rel.state === 'self';
    var theme = themeOf(d.appearance);
    wrap.style.setProperty('--p-accent', theme.accent);
    var banner = h('div', { className: 'profile-banner' + (d.appearance && d.appearance.bannerOverlay ? ' overlay' : ''), id: 'profile-banner' });
    banner.style.setProperty('--b-from', theme.from);
    banner.style.setProperty('--b-to', theme.to);
    if (d.appearance && d.appearance.bannerImage && MEDIA_ID.test(d.appearance.bannerImage)) fillBanner(banner, d.appearance.bannerImage);
    var status = d.presence ? d.presence.status : 'offline';
    var featured = d.featuredBadges || [];
    var others = (d.badges || []).map(function (b) { return b.id; }).filter(function (id) { return featured.indexOf(id) < 0 && BADGES[id]; });
    var badgeRow = featured.length || others.length ? h('div', { className: 'profile-badges', id: 'profile-badges' },
      featured.map(badgeChip), others.map(function (id) { return badgeMedal(id, 'md'); })) : null;
    var meta = [h('span', { className: 'presence-line', id: 'profile-presence' }, h('span', { className: 'dot ' + status }), presenceSentence(d.presence))];
    meta.push(h('span', { text: 'Joined ' + new Date(d.joinedAt).toLocaleDateString() }));
    if (!isSelf && d.mutualFriendCount > 0) meta.push(h('span', { text: d.mutualFriendCount + ' mutual friend' + (d.mutualFriendCount === 1 ? '' : 's') }));
    wrap.appendChild(h('section', { className: 'profile-hero', 'aria-label': 'Profile' }, banner,
      h('div', { className: 'profile-head' }, avatarNode(u, status, true),
        h('div', { className: 'profile-ident' },
          h('h1', { className: 'profile-name', id: 'profile-name', text: u.displayName || u.onlineId }),
          h('div', { className: 'profile-handle' }, h('span', { text: '@' + u.onlineId }),
            h('span', { className: 'rel-chip rel-' + rel.state, id: 'profile-relationship', text: relationshipLabel(rel) })),
          badgeRow,
          d.statusMessage ? h('p', { className: 'profile-status', id: 'profile-status', text: d.statusMessage }) : null,
          h('div', { className: 'profile-meta' }, meta),
          d.presence && d.presence.activity ? h('p', { className: 'muted small', text: d.presence.activity + ' (self-reported)' }) : null),
        profileActions(d))));
    if (d.restricted) {
      wrap.appendChild(h('div', { className: 'notice', id: 'profile-restricted' }, rel.state === 'blocked'
        ? 'You blocked this player. Unblock them on the AKENO NETWORK website to see their profile again.'
        : 'This player only shares profile details with friends.'));
    }
    var game = d.presence && d.presence.game;
    var mainCol = h('div', { className: 'profile-main' },
      game ? h('section', { className: 'pcard', id: 'profile-now-section' }, nowPlayingCard(game)) : null,
      recentGamesSection(P, isSelf),
      d.restricted ? null : profileSection('Favorite games', 'profile-favorites', (d.favoriteGameDetails || []).length
        ? h('div', { className: 'tile-row' }, d.favoriteGameDetails.slice(0, 12).map(function (f, i) { return gameCard({ name: f.name, art: f.art }, 'pg-fav-' + i, []); }))
        : h('p', { className: 'muted', text: isSelf ? 'Add favorite games in your profile settings on the AKENO NETWORK website.' : 'None listed.' })));
    var side = h('aside', { className: 'profile-side' },
      statsSection(P.activity),
      partySection(d, isSelf),
      d.restricted ? null : h('section', { className: 'panel pcard', id: 'profile-about', tabindex: '0' }, h('h2', { className: 'pcard-title', text: 'About' }),
        h('p', { className: 'bio', id: 'profile-bio', text: d.bio || (isSelf ? 'You have not written a bio yet. Add one on the AKENO NETWORK website.' : 'No bio yet.') })),
      (d.mutualFriends || []).length ? h('section', { className: 'panel pcard', id: 'profile-mutual' }, h('h2', { className: 'pcard-title', text: 'Mutual friends' }),
        h('div', { className: 'list' }, d.mutualFriends.map(function (m) {
          return h('button', { className: 'item', type: 'button', id: 'pg-mutual-' + m.id, onclick: function () { openProfile(m.id); } },
            avatarNode(m), h('span', { className: 'grow' }, h('div', { className: 'name', text: m.displayName || m.onlineId }),
              h('div', { className: 'sub', text: '@' + m.onlineId })));
        }))) : null,
      isSelf ? h('p', { className: 'muted small', text: 'Edit your picture, banner, theme, badges and bio on the AKENO NETWORK website. Who sees what is set in its privacy settings.' }) : null);
    wrap.appendChild(h('div', { className: 'profile-grid' }, mainCol, side));
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
    var qaDiag = quickAccessDiagnostics(d.quickAccess);
    if (qaDiag) wrap.appendChild(qaDiag);
    if (d.experimentalAudio && audioLab) wrap.appendChild(audioLab.section());
    wrap.appendChild(accountBridgeSection(S.app && S.app.accountBridge));
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Features' }),
      h('dl', { className: 'kv' }, d.capabilities.map(function (c) {
        return [h('dt', { text: c.feature }), h('dd', {}, h('span', { className: 'state-' + c.state, text: c.state }), ' — ' + c.reason)];
      }))));
    wrap.appendChild(h('section', { className: 'panel' }, h('h2', { text: 'Recent log (tokens are redacted)' }),
      h('div', { className: 'log mono', text: d.log.join('\n') })));
    return wrap;
  }
  // Account Bridge (docs/PS5_ACCOUNT_BRIDGE.md): links the selected local user to this AKENO
  // account via the already-paired device, and an opt-in, read-only account/Remote Play check.
  // Nothing is written to the console; the account ID value is never stored or shown.
  function accountBridgeSection(ab) {
    var out = h('div', { id: 'bridge-result' });
    function renderBridge(b) {
      clear(out);
      b = b || {};
      var rows = [['Local user', b.haveUser ? (b.userName + ' (id ' + b.localUserId + ')') : 'none selected on this console'],
        ['Note', 'Local label only — not verified by Sony, not a PSN sign-in'],
        ['Link', b.linkState ? (b.linkState + (b.linkError ? ' — ' + b.linkError : '')) : 'not linked']];
      if (b.checked) {
        rows.push(['Account ID', b.accountState || 'unknown']);
        if (b.assessment) rows.push(['Remote Play', b.assessment.code + ' — ' + b.assessment.summary]);
      }
      out.appendChild(kv(rows));
    }
    function post(path, body, btn, pending) {
      if (btn) { btn.disabled = true; }
      clear(out);
      out.appendChild(h('p', { className: 'muted', text: pending }));
      api('POST', path, body).then(function (res) {
        if (btn) { btn.disabled = false; }
        if (res && res.data) { if (S.app) { S.app.accountBridge = res.data; } renderBridge(res.data); }
        else { renderBridge(ab); out.appendChild(h('p', { className: 'error', text: (res && res.error && res.error.message) || 'The console app did not answer.' })); }
      });
    }
    renderBridge(ab);
    return h('section', { className: 'panel', id: 'account-bridge' }, h('h2', { text: 'Account Bridge (read-only)' }),
      h('p', { className: 'muted small', text: 'Links the selected local user to this AKENO account using the device you already paired. The account check runs only when you start it, reads only, and never stores or shows the account ID value. See docs/PS5_ACCOUNT_BRIDGE.md.' }),
      h('div', { className: 'row' },
        h('button', { className: 'btn secondary', type: 'button', id: 'bridge-link', text: 'Link this local user', onclick: function (ev) {
          post('/api/account-bridge/link', { check: false }, ev.target, 'Linking...');
        } }),
        h('button', { className: 'btn secondary', type: 'button', id: 'bridge-check', text: 'Run read-only account check', onclick: function (ev) {
          post('/api/account-bridge/check', undefined, ev.target, 'Checking (read-only)...');
        } }),
        h('button', { className: 'btn ghost', type: 'button', id: 'bridge-unlink', text: 'Unlink', onclick: function (ev) {
          post('/api/account-bridge/unlink', undefined, ev.target, 'Removing the link...');
        } })),
      out);
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
        toggle('set-notify-actionable', 'Message notifications with an "Open in AKENO" button', 'actionableNotifications', n.actionable),
        h('p', { className: 'muted small', text: 'Selecting the button opens the conversation in AKENO (reported working on a PS5 with firmware 12.20). If ' +
          'notifications stop appearing, turn this off; AKENO then uses the plain notifications again.' })),
      quickAccessSettings(),
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

  // ---- Quick access ---------------------------------------------------------------------------
  // How to get to AKENO while a game runs. Each way says how far it is verified; the controller
  // shortcut is only offered as an opt-in experiment until it has worked on a real PS5.
  var QUICK_METHODS = [
    ['Notification button "Open in AKENO"', 'Verified on a PS5 (12.20): selecting it opens the conversation. Needs the switch under Notifications.', 'ok'],
    ['Send the AKENO ELF again', 'Reopens this page in the AKENO that is already running (no second copy, pairing and drafts kept). Proven mechanism.', 'ok'],
    ['PS button, then the web browser (Switcher / Home)', 'The console\'s own app switching. Whether the browser keeps this page while a game runs is a test item on your PS5.', 'unknown'],
    ['Controller shortcut (experimental)', 'Not verified. A background payload may not receive controller input while a game or the browser is in front; the test below tells.', 'unknown']
  ];
  function secondsAgoText(s) { return s < 0 ? 'never' : s < 60 ? s + ' s ago' : durationText(s) + ' ago'; }
  function quickAccessSettings() {
    var q = S.app.quickAccess;
    var box = h('section', { className: 'panel', id: 'quick-access' }, h('h2', { text: 'Quick Access' }),
      h('p', { className: 'muted small', text: 'Ways to open AKENO while you play, and how far each is verified. The PS button itself always belongs to the console; AKENO never changes it.' }),
      h('div', { className: 'methods' }, QUICK_METHODS.map(function (m) {
        return h('div', { className: 'method' }, h('span', { className: 'state-pill ' + m[2], text: m[2] === 'ok' ? 'Works' : 'Untested' }),
          h('div', {}, h('div', { className: 'name', text: m[0] }), h('div', { className: 'muted small', text: m[1] })));
      })));
    if (!q) return box;
    var enable = h('input', { type: 'checkbox', id: 'qa-enabled' });
    enable.checked = !!q.enabled;
    enable.addEventListener('change', function () {
      api('POST', '/api/settings', { quickAccessShortcut: enable.checked }).then(function (res) {
        if (!res.ok) toast(res.error.message, true);
        else toast(enable.checked ? 'Controller shortcut on (experimental).' : 'Controller shortcut off.');
        refreshState();
      });
    });
    var combo = h('select', { id: 'qa-combo', 'aria-label': 'Button combination' }, q.combos.map(function (c) {
      return h('option', { value: c.id, text: c.label });
    }));
    combo.value = q.combo;
    combo.addEventListener('change', function () { api('POST', '/api/settings', { quickAccessCombo: combo.value }).then(refreshState); });
    var hold = h('select', { id: 'qa-hold', 'aria-label': 'Hold time' }, q.holds.map(function (ms) {
      return h('option', { value: String(ms), text: 'Hold ' + (ms / 1000) + ' s' });
    }));
    hold.value = String(q.holdMs);
    hold.addEventListener('change', function () { api('POST', '/api/settings', { quickAccessHoldMs: Number(hold.value) }).then(refreshState); });
    var t = q.test || {};
    var testText = t.active ? 'Test running (' + t.secondsLeft + ' s left): hold ' + q.comboLabel + ' now. Detected: ' + t.detections + '.'
      : t.detections ? 'Last test: the shortcut was detected ' + t.detections + ' time' + (t.detections === 1 ? '' : 's') + '.' : '';
    box.appendChild(h('div', { className: 'qa-shortcut' },
      h('label', { className: 'check', for: 'qa-enabled' }, enable, 'Experimental: open AKENO with a controller shortcut'),
      h('div', { className: 'row' }, combo, hold,
        h('button', { className: 'btn secondary', type: 'button', id: 'qa-test', text: 'Test the shortcut (30 s)', onclick: function () {
          api('POST', '/api/quick-access/test', { seconds: 30 }).then(function () { refreshState(); });
        } })),
      testText ? h('p', { className: 'small', id: 'qa-test-result', text: testText }) : null,
      kv([['Status', q.state + ' — ' + q.detail], ['Controller', q.library ? (q.connected ? 'connected' : 'no data yet') + ' · ' + q.reads + ' reads, ' + q.readErrors + ' errors' : 'not read'],
        ['Detections', q.detections + ' (last ' + secondsAgoText(q.lastDetectionSecondsAgo) + ')'],
        ['Last activation', q.activations ? secondsAgoText(q.lastActivationSecondsAgo) + ' · browser result ' + q.lastBrowserResult : 'never']]),
      h('p', { className: 'muted small', text: 'Off by default. While on, AKENO reads the controller state 20 times a second (read-only: no vibration, light ' +
        'bar or input changes) and opens this page when you hold the combination. It does not take input away from games, but a game may still react ' +
        'to the same buttons; OPTIONS pauses most games. Test it on the home screen first, then in a game. If AKENO cannot read the controller, ' +
        'the status says so and nothing else changes.' })));
    return box;
  }
  function quickAccessDiagnostics(q) {
    if (!q) return null;
    return h('section', { className: 'panel', id: 'diag-quick-access' }, h('h2', { text: 'Quick access: controller shortcut (experimental)' }), kv([
      ['Enabled', q.enabled ? 'yes · ' + q.comboLabel + ', hold ' + (q.holdMs / 1000) + ' s' : 'no (Settings → Quick Access)'],
      ['Verified on hardware', q.verifiedOnHardware ? 'yes' : 'no — needs a test on your PS5 while a game runs'],
      ['State', q.state + ' — ' + q.detail], ['Method', q.method],
      ['Controller library', q.library ? 'loaded' : 'not loaded'], ['scePadInit', q.initResult || '—'],
      ['User', (q.userId || '—') + (q.userSource ? ' (' + q.userSource + ')' : '')], ['scePadOpen', (q.openResult || '—') + (q.sharedHandle ? ' (already open; scePadGetHandle used)' : '')],
      ['Reads', q.reads + ' ok, ' + q.readErrors + ' failed · last result ' + (q.lastReadResult || '—')],
      ['Button data', q.samplesWithButtons + ' samples with buttons · last ' + q.lastButtons + ' ' + secondsAgoText(q.lastButtonsSecondsAgo) +
        (q.otherBitsSamples ? ' · ' + q.otherBitsSamples + ' samples with unknown bits' : '')],
      ['Detections', q.detections + ' · last ' + secondsAgoText(q.lastDetectionSecondsAgo)],
      ['Browser opened', q.activations + ' time' + (q.activations === 1 ? '' : 's') + (q.activations ? ' · last result ' + q.lastBrowserResult : '')],
      ['Conflicts', q.reads > 200 && !q.samplesWithButtons ? 'Reads succeed but never contain button presses: input probably goes only to the app in front.' : 'none seen']]));
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
        // The server sends presence as this account may see it; a visible profile follows it.
        if (S.view === 'profile' && S.profile && S.profile.data && S.profile.id === d.userId && d.presence) {
          S.profile.data.presence = d.presence;
          render();
        }
        break;
      case 'profile.changed':
        if (S.view === 'profile' && S.profile && S.profile.id === d.userId && !S.profile.loading) loadProfile(d.userId);
        if (S.me && d.userId === S.me.id) loadMe();
        break;
      case 'quickaccess.detected':
        if (d.test) toast('Controller shortcut detected. (Test: nothing was opened.)');
        if (S.view === 'settings' || S.view === 'diagnostics') refreshState();
        break;
      case 'quickaccess.opened':
        if (S.view === 'settings') refreshState();
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
      case 'friends.changed':
        loadFriends();
        if (S.view === 'profile' && S.profile && !S.profile.loading) loadProfile(S.profile.id);
        break;
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
    if (audioLab && audioLab.back()) return;
    if (S.view === 'profile') { closeProfile(); return; }
    if (S.view === 'messages' && S.openConversation) {
      var id = S.openConversation.id;
      S.openConversation = null;
      render();
      var row = $('conv-' + id);
      if (row) row.focus();
      return;
    }
    if (S.app && S.app.paired && S.view !== 'friends') { S.view = 'friends'; render(); focusFirst(); }
  }
  // Controller/keyboard use shows the focus ring even where :focus-visible is unknown (older WebKit).
  function usingController() {
    if (document.body.className.indexOf('nav-keys') < 0) document.body.className += ' nav-keys';
  }
  var lastKeyNav = 0;
  document.addEventListener('keydown', function (ev) {
    var dirs = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' };
    var t = ev.target;
    var typing = t && (t.tagName === 'INPUT' && t.type !== 'checkbox' || t.tagName === 'TEXTAREA');
    var selecting = t && t.tagName === 'SELECT';
    if (dirs[ev.key] && !(typing && (ev.key === 'ArrowLeft' || ev.key === 'ArrowRight')) && !(selecting && (ev.key === 'ArrowUp' || ev.key === 'ArrowDown'))) {
      S.input.keys = true;
      usingController();
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
          usingController();
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
      openView(tab.getAttribute('data-view'));
      tab.focus();
    });
  });
  $('who-me').addEventListener('click', function () { openProfile(null); });
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
