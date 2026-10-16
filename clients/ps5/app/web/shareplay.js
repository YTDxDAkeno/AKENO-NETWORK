/*
 * AKENO NETWORK console page: SharePlay status and the Player 2 diagnostic receiver
 * (Alpha 0.5, experimental). Kept apart from app.js; app.js calls AkenoSharePlay.view(ctx).
 *
 * Honest by design: the console cannot stream its own games and cannot turn Player 2 input into
 * a controller; this view says so and only displays what arrives. Watching a stream needs a PC
 * or phone browser (the console browser is not assumed to play WebRTC video).
 * Written without optional chaining / nullish coalescing for older WebKit builds.
 */
'use strict';
(function () {
  var BUTTONS = [['Cross/A', 0], ['Circle/B', 1], ['Square/X', 2], ['Triangle/Y', 3], ['L1', 4], ['R1', 5], ['L2', 6], ['R2', 7],
    ['Options', 9], ['L3', 10], ['R3', 11], ['Up', 12], ['Down', 13], ['Left', 14], ['Right', 15], ['Touchpad', 17]];
  var root = null;
  var ctx = null;
  var timer = null;
  var session = null;
  var sessionAt = 0;
  var receiver = null;

  function text(tag, value, cls) {
    return ctx.h(tag, { text: value, className: cls || '' });
  }

  function nameOf(userId) {
    var party = ctx.S.party;
    var members = (party && party.members) || [];
    for (var i = 0; i < members.length; i++) if (members[i].user.id === userId) return members[i].user.displayName;
    return 'Someone';
  }

  function sessionPanel() {
    var h = ctx.h;
    var panel = h('section', { className: 'panel', id: 'shareplay-session' }, h('h2', { text: 'SharePlay in your party' }));
    var party = ctx.S.party;
    if (!party) {
      panel.appendChild(text('p', 'You are not in a party. Start or join one on the AKENO website to use SharePlay.', 'muted'));
      return panel;
    }
    if (!session) {
      panel.appendChild(text('p', 'Loading…', 'muted'));
      return panel;
    }
    if (!session.available) {
      panel.appendChild(text('p', session.reason || 'SharePlay is not available on this server.', 'muted'));
      return panel;
    }
    var s = session.session;
    if (!s) {
      panel.appendChild(text('p', 'Nobody is streaming. Start SharePlay from a PC browser (capture card or PS Remote Play window).', 'muted'));
      return panel;
    }
    panel.appendChild(text('p', 'Streaming: ' + nameOf(s.hostId) + ' · ' + s.quality + ' · ' + s.state + ' · ' + s.viewerIds.length + '/' + s.maxViewers + ' watching'));
    var grant = s.controller && s.controller.grant;
    panel.appendChild(text('p', grant ? 'Player 2: ' + nameOf(grant.guestId) + (grant.inputConnected ? ' (controller connected)' : ' (controller not connected)') : 'Player 2: nobody', 'muted'));
    panel.appendChild(text('p', 'To watch, open the party on a PC or phone. Approving or revoking Player 2 is done on the website.', 'muted'));
    return panel;
  }

  function receiverPanel() {
    var h = ctx.h;
    var panel = h('section', { className: 'panel', id: 'shareplay-receiver' }, h('h2', { text: 'Player 2 diagnostic receiver' }));
    if (!receiver) {
      panel.appendChild(text('p', 'Loading…', 'muted'));
      return panel;
    }
    var r = receiver.receiver;
    var caps = receiver.capabilities;
    panel.appendChild(text('p', caps.virtualController.reason, 'warn'));
    if (!r.grantId) {
      panel.appendChild(text('p', 'No Player 2 input received since the app started.', 'muted'));
    } else {
      var pressed = BUTTONS.filter(function (b) { return (r.buttons & (1 << b[1])) !== 0; }).map(function (b) { return b[0]; });
      panel.appendChild(text('p', r.neutralByWatchdog ? 'No fresh input for more than ' + r.watchdogMs + ' ms: everything released (neutral).' : 'Receiving input (' + r.packetsPerSecond + ' per second).', r.neutralByWatchdog ? 'muted' : ''));
      var list = h('ul', { className: 'kv', id: 'shareplay-state' });
      list.setAttribute('data-buttons', String(r.buttons));
      [['Buttons', pressed.length ? pressed.join(', ') : 'none'],
        ['Left stick', r.axes[0] + ', ' + r.axes[1]],
        ['Right stick', r.axes[2] + ', ' + r.axes[3]],
        ['Triggers', 'L2 ' + r.triggers[0] + ' · R2 ' + r.triggers[1]],
        ['Last packet', r.ageMs < 0 ? 'never' : r.ageMs + ' ms ago (#' + r.lastSeq + ')'],
        ['Accepted / stale / invalid', r.accepted + ' / ' + r.stale + ' / ' + r.invalid],
        ['Releases / watchdog', r.releases + ' / ' + r.watchdogTrips]].forEach(function (row) {
        list.appendChild(h('li', h('span', { text: row[0] }), h('strong', { text: row[1] })));
      });
      panel.appendChild(list);
      if (r.lastReason) panel.appendChild(text('p', r.lastReason, 'muted'));
    }
    panel.appendChild(text('p', 'Streaming from this console: ' + caps.nativeCapture.reason, 'muted'));
    return panel;
  }

  function paint() {
    if (!root) return;
    while (root.firstChild) root.removeChild(root.firstChild);
    root.appendChild(sessionPanel());
    root.appendChild(receiverPanel());
  }

  function poll() {
    if (!root || !document.body.contains(root)) {
      clearInterval(timer);
      timer = null;
      root = null;
      return;
    }
    ctx.api('GET', '/api/shareplay').then(function (res) {
      if (res && res.ok) receiver = res.data;
      paint();
    });
    var party = ctx.S.party;
    if (party && Date.now() - sessionAt > 3000) {
      sessionAt = Date.now();
      ctx.server('GET', '/parties/' + party.id + '/shareplay', undefined, true).then(function (data) {
        if (data) session = data;
        paint();
      });
    }
  }

  window.AkenoSharePlay = {
    view: function (context) {
      ctx = context;
      root = ctx.h('div', { className: 'stack', id: 'shareplay-view' });
      sessionAt = 0;
      paint();
      if (!timer) timer = setInterval(poll, 250);
      poll();
      return root;
    },
  };
})();
