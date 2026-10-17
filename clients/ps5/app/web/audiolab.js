/*
 * AKENO NETWORK console page: the audio lab v3 panel (PS5 audio research, docs/PS5_AUDIO_RESEARCH.md).
 * Served only by the experimental audio lab build; the release build serves an empty /audiolab.js,
 * so its page contains none of this. Every check starts when the user selects it, the tone needs a
 * confirmation and nothing is recorded. "Audible" is only shown after the user says they heard it.
 * Written without optional chaining / nullish coalescing for older WebKit builds.
 */
'use strict';
window.AkenoAudioLab = function (ctx) {
  var h = ctx.h, $ = ctx.$, api = ctx.api, kv = ctx.kv, toast = ctx.toast;
  var L = { preset: '', level: 'moderate', confirm: null, report: '', polling: false, status: null, loading: false, browserPending: null, mediaPending: null, mediaRunning: false };

  // ---- Audio lab v3 (audio lab builds only; docs/PS5_AUDIO_RESEARCH.md) -----------------------
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
  // Read-only feature detection of the browser's own audio (nothing is played, opened or asked for).
  var MEDIA_TYPES = [['WAV', 'audio/wav'], ['WAV (PCM codec)', 'audio/wav; codecs="1"'], ['MP3', 'audio/mpeg'], ['AAC (MP4)', 'audio/mp4; codecs="mp4a.40.2"'],
    ['Ogg Vorbis', 'audio/ogg; codecs="vorbis"'], ['Ogg Opus', 'audio/ogg; codecs="opus"'], ['WebM Opus', 'audio/webm; codecs="opus"']];
  function canPlay(audio, type) {
    try { return (audio && audio.canPlayType && audio.canPlayType(type)) || 'no'; } catch (e) { return 'error'; }
  }
  function browserAudioFeatures() {
    var audio = null;
    try { audio = document.createElement('audio'); } catch (e) { audio = null; }
    var md = navigator.mediaDevices;
    var ms = window.MediaSource || window.WebKitMediaSource;
    var rows = [['Web Audio (AudioContext)', window.AudioContext ? 'available' : window.webkitAudioContext ? 'available (webkit prefix)' : 'not available'],
      ['OfflineAudioContext', window.OfflineAudioContext || window.webkitOfflineAudioContext ? 'present' : 'not present'],
      ['HTML audio element', typeof window.HTMLAudioElement === 'undefined' ? 'not present' : audio && typeof audio.play === 'function' ? 'present (play() exists)' : 'present, no play()'],
      ['Blob URLs', window.Blob && window.URL && URL.createObjectURL ? 'available' : 'not available']];
    MEDIA_TYPES.forEach(function (t) { rows.push(['canPlayType ' + t[0], canPlay(audio, t[1])]); });
    rows.push(['Media Source Extensions', ms ? 'present' + (ms.isTypeSupported ? ' · AAC ' + (ms.isTypeSupported('audio/mp4; codecs="mp4a.40.2"') ? 'yes' : 'no') +
      ' · WebM Opus ' + (ms.isTypeSupported('audio/webm; codecs="opus"') ? 'yes' : 'no') : '') : 'not present']);
    rows.push(['Microphone API (getUserMedia)', md && md.getUserMedia ? 'present (not used)' : window.navigator.webkitGetUserMedia ? 'webkit prefix only (not used)' : 'not present']);
    rows.push(['WebRTC (RTCPeerConnection)', window.RTCPeerConnection || window.webkitRTCPeerConnection ? 'present (not used)' : 'not present']);
    rows.push(['User agent', String(navigator.userAgent || '').slice(0, 160)]);
    return rows;
  }
  function featureLine() {
    return browserAudioFeatures().map(function (r) { return r[0] + ': ' + r[1]; }).join('; ').slice(0, 470);
  }
  // The same tone as the native test, as a 48 kHz 16-bit stereo WAV generated here (no server route).
  function toneWav(level) {
    var rate = 48000, lead = rate / 4, tone = rate * 3 / 2, total = lead + tone + rate / 4, ramp = rate / 100;
    var buf = new ArrayBuffer(44 + total * 4), v = new DataView(buf);
    function str(at, s) { for (var i = 0; i < s.length; i++) v.setUint8(at + i, s.charCodeAt(i)); }
    str(0, 'RIFF'); v.setUint32(4, 36 + total * 4, true); str(8, 'WAVE'); str(12, 'fmt '); v.setUint32(16, 16, true);
    v.setUint16(20, 1, true); v.setUint16(22, 2, true); v.setUint32(24, rate, true); v.setUint32(28, rate * 4, true);
    v.setUint16(32, 4, true); v.setUint16(34, 16, true); str(36, 'data'); v.setUint32(40, total * 4, true);
    var amp = Math.min(Math.max(level, 0), 0.5) * 32767;
    for (var n = 0; n < tone; n++) {
      var g = Math.min(1, n / ramp, (tone - 1 - n) / ramp);
      var s = Math.round(amp * g * Math.sin(2 * Math.PI * 440 * n / rate));
      v.setInt16(44 + (lead + n) * 4, s, true);
      v.setInt16(44 + (lead + n) * 4 + 2, s, true);
    }
    return buf;
  }
  function base64(buf) {
    var bytes = new Uint8Array(buf), out = '', chunk = 0x8000;
    for (var i = 0; i < bytes.length; i += chunk) out += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    return window.btoa(out);
  }
  // Plays the WAV with an <audio> element. Must start inside the button press (autoplay rules).
  function playMediaTone(level, done) {
    var result = { source: '', playResult: '', loadedMetadata: false, canPlay: false, playing: false, ended: false, errorCode: 0, duration: -1, currentTime: -1, features: featureLine() };
    var audio, url = '', finished = false;
    function finish() {
      if (finished) return;
      finished = true;
      try {
        result.currentTime = isFinite(audio.currentTime) ? audio.currentTime : -1;
        result.duration = isFinite(audio.duration) ? audio.duration : -1;
        if (audio.error && audio.error.code) result.errorCode = audio.error.code;
        audio.pause();
      } catch (e) { /* the element is gone */ }
      if (url && result.source === 'blob') { try { URL.revokeObjectURL(url); } catch (e) { /* ignore */ } }
      done(result);
    }
    try {
      audio = document.createElement('audio');
      if (typeof audio.play !== 'function') { result.playResult = 'no play() on the audio element'; done(result); return; }
      var wav = toneWav(level);
      if (window.Blob && window.URL && URL.createObjectURL) {
        url = URL.createObjectURL(new Blob([wav], { type: 'audio/wav' }));
        result.source = 'blob';
      } else {
        url = 'data:audio/wav;base64,' + base64(wav);
        result.source = 'data';
      }
      audio.preload = 'auto';
      audio.addEventListener('loadedmetadata', function () { result.loadedMetadata = true; });
      audio.addEventListener('canplay', function () { result.canPlay = true; });
      audio.addEventListener('playing', function () { result.playing = true; });
      audio.addEventListener('ended', function () { result.ended = true; setTimeout(finish, 100); });
      audio.addEventListener('error', function () { setTimeout(finish, 100); });
      audio.src = url;
      var p = audio.play();
      if (p && typeof p.then === 'function') {
        p.then(function () { result.playResult = 'resolved'; }, function (e) { result.playResult = 'rejected: ' + String((e && (e.name || e.message)) || e).slice(0, 50); });
      } else {
        result.playResult = 'no promise';
      }
      setTimeout(finish, 4000);
    } catch (e) {
      result.playResult = 'exception: ' + String((e && e.message) || e).slice(0, 50);
      done(result);
    }
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
  // Measured: results, raw bytes, written bytes, canaries. Decoded fields are the PS4 layout, unverified.
  function portStateSummary(r) {
    var ps = r.portState;
    if (!r.openCalled || !(r.handle > 0)) return 'not read (no port was opened)';
    if (!ps || !ps.exported) return 'not read: sceAudioOutGetPortState is not exported';
    return ps.points.map(function (p) {
      if (!p.attempted) return p.point + ': not measured';
      var d = p.decoded || {};
      return p.point + ': ' + p.calls.map(function (c) { return hexResult(c.result); }).join(' / ') + ', ' + p.writtenBytes + ' bytes written' +
        (p.guardsIntact ? '' : ' (WROTE OUTSIDE ITS WINDOW)') + (d.output !== null && d.output !== undefined ? ' · decoded (unverified): output ' + d.output + ' (' + d.outputNames + ')' +
        (d.channel !== null ? ', channel ' + d.channel : '') + (d.volume !== null ? ', volume ' + d.volume : '') : '');
    }).join(' | ') + (ps.stopped ? ' · stopped for this launch' : '');
  }
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
      ['Port state', portStateSummary(r)],
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
    var box = h('section', { className: 'panel', id: 'audio-lab' }, h('h2', { text: 'Audio lab v3 (experimental build)' }),
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
      h('button', { className: 'btn secondary', type: 'button', id: 'audio-media', text: 'Browser <audio> test…', onclick: function () { L.confirm = 'media'; labRefresh(); focusId('audio-media-play'); } }),
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
    if (L.confirm === 'media') {
      var mlevel = h('select', { id: 'audio-media-level', 'aria-label': 'Level' }, a.levels.map(function (l) { return h('option', { value: l.id, text: l.label }); }));
      mlevel.value = L.level;
      box.appendChild(h('div', { className: 'confirm', id: 'audio-media-confirm' },
        h('p', { text: 'Play the same 1.5 s, 440 Hz tone as a WAV file through the browser\'s HTML <audio> element (the system browser has no Web Audio API)?' }),
        h('div', { className: 'row' }, mlevel,
          h('button', { className: 'btn', type: 'button', id: 'audio-media-play', text: 'Play with <audio>', onclick: function () {
            L.confirm = null;
            L.level = mlevel.value;
            L.mediaRunning = true;
            playMediaTone(LAB_LEVELS[mlevel.value] || 0.1, function (result) {
              L.mediaRunning = false;
              L.mediaPending = result;
              labPost('/api/diagnostics/audio/media', result).then(function () { focusId('audio-mheard-tv'); });
            });
            labRefresh();
          } }),
          h('button', { className: 'btn ghost', type: 'button', text: 'Cancel', onclick: function () { L.confirm = null; labRefresh(); } }))));
    }
    if (L.mediaRunning) box.appendChild(h('div', { className: 'confirm', id: 'audio-media-running' }, h('p', { className: 'name', text: 'Playing the tone with the <audio> element…' })));
    if (L.mediaPending) {
      var m = L.mediaPending;
      box.appendChild(h('div', { className: 'confirm', id: 'audio-media-feedback' },
        h('p', { className: 'name', text: '<audio> tone (' + (m.source || 'no') + ' URL): play() ' + (m.playResult || 'not reached') + (m.ended ? ', played to the end' : m.playing ? ', started' : ', never started') +
          (m.errorCode ? ', media error ' + m.errorCode : '') + '. Did you hear it?' }),
        feedbackButtons('audio-mheard-', function (heard, route) {
          var report = {};
          for (var k in m) if (Object.prototype.hasOwnProperty.call(m, k)) report[k] = m[k];
          report.heard = heard;
          report.route = route;
          L.mediaPending = null;
          labPost('/api/diagnostics/audio/media', report);
        })));
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
    box.appendChild(h('h3', { text: 'Browser audio (read-only feature check; nothing here plays or opens a microphone)' }));
    var browserRows = browserAudioFeatures();
    if (a.browser) {
      browserRows.push(['Last browser tone', a.browser.state + (a.browser.sampleRate ? ', ' + a.browser.sampleRate + ' Hz' : '') + (a.browser.clockAdvanced ? ', clock running' : '') +
        (a.browser.heard === true ? ' · HEARD on ' + a.browser.route : a.browser.heard === false ? ' · not heard' : '') + (a.browser.error ? ' · ' + a.browser.error : '')]);
    }
    if (a.media) {
      browserRows.push(['Last <audio> tone', (a.media.source || '?') + ' URL · play() ' + (a.media.playResult || '—') + ' · ' + (a.media.ended ? 'played to the end' : a.media.playing ? 'started' : 'never started') +
        (a.media.errorCode ? ' · media error ' + a.media.errorCode : '') + (a.media.heard === true ? ' · HEARD on ' + a.media.route : a.media.heard === false ? ' · not heard' : '')]);
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
