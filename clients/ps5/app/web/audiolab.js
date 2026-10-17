/*
 * AKENO NETWORK console page: the audio lab v2 panel (PS5 audio research, docs/PS5_AUDIO_RESEARCH.md).
 * Served only by the experimental audio lab build; the release build serves an empty /audiolab.js,
 * so its page contains none of this. Every check starts when the user selects it, the tone needs a
 * confirmation and nothing is recorded. "Audible" is only shown after the user says they heard it.
 * Written without optional chaining / nullish coalescing for older WebKit builds.
 */
'use strict';
window.AkenoAudioLab = function (ctx) {
  var h = ctx.h, $ = ctx.$, api = ctx.api, kv = ctx.kv, toast = ctx.toast;
  var L = { preset: '', level: 'moderate', confirm: null, report: '', polling: false, status: null, loading: false, browserPending: null };

  // ---- Audio lab v2 (audio lab builds only; docs/PS5_AUDIO_RESEARCH.md) -----------------------
  // Every check starts when the user selects it; the tone needs a confirmation; nothing is
  // recorded. "Audible" is only ever shown after the user says they heard the tone.
  var LAB_STATES = {
    'not-run': ['No output test yet', ''], 'not-supported': ['Not supported', 'bad'], 'init-failed': ['Failed to initialize', 'bad'],
    'open-failed': ['Failed to open an output port', 'bad'], 'submit-failed': ['Failed to submit samples', 'bad'],
    accepted: ['Samples accepted — audible output NOT confirmed', 'warn'], 'accepted-not-heard': ['Samples accepted, but nothing was heard', 'warn'],
    'audible-confirmed': ['Audible output confirmed by you', 'ok'], cancelled: ['Cancelled', ''], waiting: ['Waiting to start', 'warn'],
    playing: ['Playing', 'warn'], 'not-started': ['Could not start', 'bad']
  };
  var ROUTES = [['tv', 'TV / HDMI'], ['controller-headset', 'Headset on the controller'], ['usb-headset', 'USB headset'], ['other', 'Somewhere else']];
  var LAB_LEVELS = { quiet: 0.05, moderate: 0.1, clear: 0.2 };
  function labRefresh(res) {
    if (res && res.ok && res.audio) L.status = res.audio;
    else if (res && !res.ok) toast((res.error && res.error.message) || 'The audio lab did not answer.', true);
    var old = $('audio-lab');
    if (!old) return;
    var focused = document.activeElement && document.activeElement.id;
    old.parentNode.replaceChild(buildLab(), old);
    var el = focused && $(focused);
    if (el) el.focus();
    else if (focused && old.querySelector('#' + focused)) { var first = $('audio-lab').querySelector('button'); if (first) first.focus(); }
    var st = L.status;
    if (st && st.running && !L.polling) {
      L.polling = true;
      setTimeout(function () { L.polling = false; api('GET', '/api/diagnostics/audio').then(labRefresh); }, 500);
    }
  }
  function labPost(path, body) { return api('POST', path, body).then(function (res) { labRefresh(res); return res; }); }
  function audioLabSection() {
    if (!L.status && !L.loading) {
      L.loading = true;
      api('GET', '/api/diagnostics/audio').then(function (res) { L.loading = false; labRefresh(res); });
    }
    return buildLab();
  }
  // Read-only feature detection of the browser's own audio (nothing is called).
  function browserAudioFeatures() {
    var audio = document.createElement('audio');
    var md = navigator.mediaDevices;
    return [['Web Audio (AudioContext)', window.AudioContext ? 'available' : window.webkitAudioContext ? 'available (webkit prefix)' : 'not available'],
      ['Audio element (WAV)', audio.canPlayType ? audio.canPlayType('audio/wav') || 'no' : 'no'],
      ['Microphone API (getUserMedia)', md && md.getUserMedia ? 'present (not used)' : 'not present'],
      ['WebRTC (RTCPeerConnection)', window.RTCPeerConnection || window.webkitRTCPeerConnection ? 'present (not used)' : 'not present']];
  }
  // The browser's own test tone (Web Audio). Must start inside the button press.
  function playBrowserTone(level, done) {
    var Ctx = window.AudioContext || window.webkitAudioContext;
    var result = { apiAvailable: !!Ctx, state: '', sampleRate: 0, clockAdvanced: false, error: '' };
    if (!Ctx) { result.state = 'unavailable'; done(result); return; }
    try {
      var ctx = new Ctx(), osc = ctx.createOscillator(), gain = ctx.createGain();
      var t0 = ctx.currentTime;
      osc.type = 'sine';
      osc.frequency.value = 440;
      gain.gain.setValueAtTime(0, t0);
      gain.gain.linearRampToValueAtTime(level, t0 + 0.02);
      gain.gain.setValueAtTime(level, t0 + 1.48);
      gain.gain.linearRampToValueAtTime(0, t0 + 1.5);
      osc.connect(gain);
      gain.connect(ctx.destination);
      if (ctx.resume) ctx.resume();
      osc.start(t0);
      osc.stop(t0 + 1.5);
      result.sampleRate = ctx.sampleRate || 0;
      setTimeout(function () {
        result.state = ctx.state || 'unknown';
        result.clockAdvanced = ctx.currentTime > t0 + 0.5;
        try { if (ctx.close) ctx.close(); } catch (e) { /* already closed */ }
        done(result);
      }, 1800);
    } catch (e) {
      result.state = 'error';
      result.error = String((e && e.message) || e).slice(0, 110);
      done(result);
    }
  }
  function feedbackButtons(prefix, onAnswer) {
    return h('div', { className: 'row' }, ROUTES.map(function (r) {
      return h('button', { className: 'btn', type: 'button', id: prefix + r[0], text: 'Yes — ' + r[1], onclick: function () { onAnswer(true, r[0]); } });
    }), h('button', { className: 'btn secondary', type: 'button', id: prefix + 'no', text: 'No, I heard nothing', onclick: function () { onAnswer(false, ''); } }));
  }
  function hexResult(value) { return value === '0x00000000' ? '0 (success)' : value; }
  function runDetails(r, a) {
    var probe = a.probe;
    var out = probe && probe.libraries[0];
    return kv([
      ['Firmware', a.firmware || 'unknown'],
      ['Audio library', out ? out.name + ': ' + (out.loaded ? 'loads' : 'does not load (' + out.error + ')') : r.libraryLoaded ? 'libSceAudioOut.sprx loaded' : 'not loaded'],
      ['Exported functions', out ? out.symbols.map(function (x) { return x.name + (x.exported ? ' ✓' : ' ✗'); }).join(', ') : 'run "Check audio libraries"'],
      ['sceAudioOutInit', r.libraryLoaded ? hexResult(r.initResult) + (r.initCalled ? '' : ' (from an earlier test; called once per launch)') : 'not called'],
      ['Requested user ID', r.userId + ' (system user, as the SDL driver)'], ['Port type', r.portType + ' (main)'],
      ['Sample rate', r.rate + ' Hz'], ['Channels', String(r.channels)], ['Sample format', r.format + ' (param ' + r.formatParam + ')'],
      ['Frames per call', String(r.grain)],
      ['sceAudioOutOpen', r.openCalled ? hexResult(r.openResult) + (r.handle > 0 ? ' → port handle ' + r.handle : '') : 'not called'],
      ['Port state', a.portState],
      ['sceAudioOutOutput', r.calls ? r.callsOk + ' of ' + r.calls + ' calls succeeded' + (r.firstErrorCall ? ' · first error ' + r.firstErrorResult + ' (call ' + r.firstErrorCall + ')' : '') +
        ' · last result ' + hexResult(r.lastOutputResult) : 'not called'],
      ['Samples submitted', r.framesSubmitted + ' frames = ' + r.submittedMs + ' ms (tone ' + r.toneMs + ' ms at ' + r.toneHz + ' Hz, ' + r.leadMs + ' ms silence before, ' + r.tailMs + ' ms after)'],
      ['PCM contains sound', r.pcmNonzero ? 'yes · ' + r.nonzeroFrames + ' nonzero frames, peak ' + r.peak.toFixed(3) : 'NO'],
      ['Timing', r.calls > 1 ? r.wallMs.toFixed(0) + ' ms in total; per call ' + r.callMinMs.toFixed(1) + ' / ' + r.callAvgMs.toFixed(1) + ' / ' + r.callMaxMs.toFixed(1) +
        ' ms (min/avg/max), one block = ' + r.blockMs.toFixed(1) + ' ms → ' + (r.paced === true ? 'consumed at playback speed' : r.paced === false ? 'returned faster than real time' : 'too few calls to judge') : '—'],
      ['Buffer', r.bufferAligned ? '64-byte aligned static buffer' : 'not aligned'],
      ['sceAudioOutClose', r.closeCalled ? hexResult(r.closeResult) : 'not called'],
      ['Errors', r.error || 'none'],
      ['Routing', a.routing]]);
  }
  function buildLab() {
    var a = L.status;
    var box = h('section', { className: 'panel', id: 'audio-lab' }, h('h2', { text: 'Audio lab v2 (experimental build)' }),
      h('p', { className: 'muted small', text: 'For PS5 audio research only. Each check runs when you select it; no audio is recorded or sent anywhere. ' +
        'Run the tests from the home screen, not during a game.' }));
    if (!a) { box.appendChild(h('p', { className: 'muted', text: 'Loading...' })); return box; }
    var st = LAB_STATES[a.state] || [a.state, ''];
    box.appendChild(h('div', { className: 'lab-state ' + st[1], id: 'audio-state' }, h('div', { className: 'lab-state-title', text: st[0] }),
      a.stateRun ? h('div', { className: 'small', text: 'Test #' + a.stateRun + (a.runsLeft >= 0 ? ' · ' + a.runsLeft + ' tests left in this launch' : '') }) : null));
    var idle = !a.running;
    box.appendChild(h('div', { className: 'row mt' },
      h('button', { className: 'btn secondary', type: 'button', id: 'audio-probe', text: 'Check audio libraries', onclick: function () { labPost('/api/diagnostics/audio/probe'); } }),
      idle ? h('button', { className: 'btn', type: 'button', id: 'audio-tone', text: 'Play a test tone…', onclick: function () { L.confirm = 'native'; labRefresh(); focusId('audio-tone-play'); } }) : null,
      idle ? h('button', { className: 'btn secondary', type: 'button', id: 'audio-tone-delayed', text: 'Play in ' + a.delaySeconds + ' s…', onclick: function () {
        L.confirm = 'delayed'; labRefresh(); focusId('audio-tone-play'); } }) : null,
      h('button', { className: 'btn secondary', type: 'button', id: 'audio-browser', text: 'Browser audio test…', onclick: function () { L.confirm = 'browser'; labRefresh(); focusId('audio-browser-play'); } }),
      h('button', { className: 'btn ghost', type: 'button', id: 'audio-report-btn', text: 'Show report', onclick: function () {
        api('GET', '/api/diagnostics/audio/report').then(function (res) { L.report = res.ok && res.text ? res.text : ''; labRefresh(); focusId('audio-report'); });
      } })));
    if (L.confirm === 'native' || L.confirm === 'delayed') {
      var preset = h('select', { id: 'audio-preset', 'aria-label': 'Output format' }, a.presets.map(function (p) { return h('option', { value: p.id, text: p.label }); }));
      preset.value = L.preset || a.presets[0].id;
      preset.addEventListener('change', function () { L.preset = preset.value; });
      var level = h('select', { id: 'audio-level', 'aria-label': 'Level' }, a.levels.map(function (l) { return h('option', { value: l.id, text: l.label }); }));
      level.value = L.level;
      level.addEventListener('change', function () { L.level = level.value; });
      var delayed = L.confirm === 'delayed';
      box.appendChild(h('div', { className: 'confirm', id: 'audio-tone-confirm' },
        h('p', { text: delayed ? 'After you confirm, AKENO waits ' + a.delaySeconds + ' s and then plays a 1.5 s, 440 Hz tone on the console\'s main audio output. ' +
          'Use the wait to press the PS button and go to the home screen (do not start a game): this tests whether audio is heard when the browser is not in front.'
          : 'Play a 1.5 s, 440 Hz tone on the console\'s main audio output now?' }),
        h('div', { className: 'row' }, preset, level),
        h('div', { className: 'row mt' },
          h('button', { className: 'btn', type: 'button', id: 'audio-tone-play', text: delayed ? 'Start (' + a.delaySeconds + ' s)' : 'Play', onclick: function () {
            L.confirm = null;
            labPost('/api/diagnostics/audio/tone', { confirm: 'play', preset: preset.value, level: level.value, delaySeconds: delayed ? a.delaySeconds : 0 });
          } }),
          h('button', { className: 'btn ghost', type: 'button', id: 'audio-tone-cancel', text: 'Cancel', onclick: function () { L.confirm = null; labRefresh(); focusId('audio-tone'); } }))));
    }
    if (L.confirm === 'browser') {
      var blevel = h('select', { id: 'audio-browser-level', 'aria-label': 'Level' }, a.levels.map(function (l) { return h('option', { value: l.id, text: l.label }); }));
      blevel.value = L.level;
      box.appendChild(h('div', { className: 'confirm', id: 'audio-browser-confirm' },
        h('p', { text: 'Play a 1.5 s, 440 Hz tone with the browser\'s own audio (Web Audio API), for comparison with the console app\'s native output?' }),
        h('div', { className: 'row' }, blevel,
          h('button', { className: 'btn', type: 'button', id: 'audio-browser-play', text: 'Play in the browser', onclick: function () {
            L.confirm = null;
            L.level = blevel.value;
            playBrowserTone(LAB_LEVELS[blevel.value] || 0.1, function (result) {
              L.browserPending = result;
              labPost('/api/diagnostics/audio/browser', result).then(function () { focusId('audio-bheard-tv'); });
            });
            labRefresh();
          } }),
          h('button', { className: 'btn ghost', type: 'button', text: 'Cancel', onclick: function () { L.confirm = null; labRefresh(); } }))));
    }
    if (a.running) {
      box.appendChild(h('div', { className: 'confirm', id: 'audio-running' },
        h('p', { className: 'name', text: a.playing ? 'Playing the test tone…' : 'Starting in ' + a.secondsUntilStart + ' s — go to the home screen now (PS button). Do not start a game.' }),
        h('button', { className: 'btn secondary', type: 'button', id: 'audio-cancel', text: 'Cancel the test', onclick: function () { labPost('/api/diagnostics/audio/cancel'); } })));
    }
    var latest = a.runs && a.runs[0];
    if (latest && !a.running && latest.heard === null && latest.calls > 0) {
      box.appendChild(h('div', { className: 'confirm', id: 'audio-feedback' }, h('p', { className: 'name', text: 'Did you hear the tone of test #' + latest.id + '?' }),
        feedbackButtons('audio-heard-', function (heard, route) { labPost('/api/diagnostics/audio/feedback', { run: latest.id, heard: heard, route: route }); })));
    }
    if (L.browserPending) {
      var b = L.browserPending;
      box.appendChild(h('div', { className: 'confirm', id: 'audio-browser-feedback' },
        h('p', { className: 'name', text: 'Browser tone: ' + (b.apiAvailable ? 'Web Audio ' + b.state + (b.clockAdvanced ? ', audio clock running' : ', audio clock NOT running') : 'Web Audio not available') +
          '. Did you hear it?' }),
        b.apiAvailable ? feedbackButtons('audio-bheard-', function (heard, route) {
          var report = { apiAvailable: b.apiAvailable, state: b.state, sampleRate: b.sampleRate, clockAdvanced: b.clockAdvanced, error: b.error, heard: heard, route: route };
          L.browserPending = null;
          labPost('/api/diagnostics/audio/browser', report);
        }) : h('button', { className: 'btn secondary', type: 'button', id: 'audio-bheard-ok', text: 'OK', onclick: function () { L.browserPending = null; labRefresh(); } })));
    }
    if (latest) {
      box.appendChild(h('h3', { text: 'Test #' + latest.id + ': ' + latest.presetLabel + ', ' + latest.levelLabel + (latest.delaySeconds ? ', delayed ' + latest.delaySeconds + ' s' : '') +
        ' — ' + (LAB_STATES[latest.outcome] ? LAB_STATES[latest.outcome][0] : latest.outcome) +
        (latest.heard === true ? ' · HEARD on ' + latest.route : latest.heard === false ? ' · not heard' : '') }));
      box.appendChild(runDetails(latest, a));
      if (a.runs.length > 1) {
        box.appendChild(h('h3', { text: 'Earlier tests' }));
        box.appendChild(h('div', { className: 'log mono', id: 'audio-runs', text: a.runs.slice(1).map(function (r) {
          return '#' + r.id + ' ' + r.preset + ' ' + r.level + (r.delaySeconds ? ' delayed' : '') + ': ' + r.outcome + ', ' + r.callsOk + '/' + r.calls + ' calls, ' +
            (r.paced === true ? 'paced' : r.paced === false ? 'not paced' : 'pace unknown') + ', ' + (r.heard === true ? 'heard (' + r.route + ')' : r.heard === false ? 'not heard' : 'no report');
        }).join('\n') }));
      }
    }
    if (a.probe) {
      var rows = [['Library folder', a.probe.libraryDir + (a.probe.libraryDirReadable ? '' : ' (not readable)')], ['Audio-related files', (a.probe.audioFiles || []).join(', ') || 'none found']];
      a.probe.libraries.forEach(function (lib) {
        rows.push([lib.name + ' (' + lib.use + ')', (lib.loaded ? 'loads' : 'does not load: ' + lib.error) + ' · ' + lib.symbols.map(function (sym) {
          return sym.name + (sym.exported ? ' ✓' : ' ✗');
        }).join(', ')]);
      });
      rows.push(['Microphone', a.probe.microphoneEnumeration]);
      box.appendChild(h('h3', { text: 'Audio libraries (read-only check)' }));
      box.appendChild(kv(rows));
    }
    box.appendChild(h('h3', { text: 'Browser audio (read-only feature check)' }));
    var browserRows = browserAudioFeatures();
    if (a.browser) {
      browserRows.push(['Last browser tone', a.browser.state + (a.browser.sampleRate ? ', ' + a.browser.sampleRate + ' Hz' : '') + (a.browser.clockAdvanced ? ', clock running' : '') +
        (a.browser.heard === true ? ' · HEARD on ' + a.browser.route : a.browser.heard === false ? ' · not heard' : '') + (a.browser.error ? ' · ' + a.browser.error : '')]);
    }
    box.appendChild(kv(browserRows));
    if (L.report) {
      box.appendChild(h('h3', { text: 'Report (no tokens or account data; photograph or copy it)' }));
      box.appendChild(h('pre', { className: 'log mono report', id: 'audio-report', tabindex: '0', text: L.report }));
    }
    box.appendChild(h('p', { className: 'muted small', text: 'The report is also saved on the console as ' + a.reportFile + '.' }));
    return box;
  }
  function focusId(id) { var el = $(id); if (el) el.focus(); }

  return {
    section: audioLabSection,
    // Circle/Escape closes an open confirmation first. Returns true when it did.
    back: function () {
      if (!L.confirm) return false;
      L.confirm = null;
      labRefresh();
      focusId('audio-probe');
      return true;
    }
  };
};
