// Diorama - the camera calibration page (calibrate.html): the camera, the board's corners drawn, the views kept; at 15
// views it calibrates, keeps the result on this device (the game reads it) and offers camera.json for the editor.
(function () {
  'use strict';
  var WANT = 15;
  var $ = function (id) { return document.getElementById(id); };
  var video = $('cam'), dots = $('dots'), status = $('status');
  var cal = null, result = null, last = 0;
  function say(t) { status.textContent = t; }
  function show(r) {
    result = r;
    say(r.ok ? 'Done: ' + r.views + ' views.' : 'Done.');
    $('result').hidden = false;
    if (!r.ok) { $('numbers').textContent = 'That didn\'t work: try again with more different views.'; $('download').hidden = true; return; }
    $('download').hidden = false;
    $('numbers').textContent = r.width + ' x ' + r.height + ': field of view ' + r.fovDeg.toFixed(1) + '° across, error ' + r.rms.toFixed(2) +
      ' px' + (r.rms > 1 ? ' (high: hold the board flat and still, and try again)' : '') + '. Saved on this device: the game uses it now.';
    try { localStorage.setItem('dio-camera', JSON.stringify(json(r))); } catch (e) {}
  }
  function json(r) { return { 'diorama-camera': 1, width: r.width, height: r.height, K: r.K, dist: r.dist, rms: r.rms, views: r.views }; }
  $('download').onclick = function () {
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(json(result), null, 1)], { type: 'application/json' }));
    a.download = 'camera.json'; a.click();
  };
  $('again').onclick = function () { cal = new window.DioramaCalibrate.Calibrator(window.cv); $('result').hidden = true; $('fill').style.width = '0'; };
  function frame(t) {
    requestAnimationFrame(frame);
    if (!cal || !$('result').hidden || video.readyState < 2 || t - last < 200) return;   // five looks a second
    last = t;
    var r = cal.addView(video), n = cal.views();
    // the corners found, where the picture is on the screen
    var vw = video.videoWidth, vh = video.videoHeight, sw = window.innerWidth, sh = window.innerHeight, k = Math.min(sw / vw, sh / vh);
    dots.width = sw; dots.height = sh;
    var g = dots.getContext('2d'); g.clearRect(0, 0, sw, sh);
    g.fillStyle = r.kept ? '#5fd35f' : '#007bb7';
    (r.points || []).forEach(function (p) { g.beginPath(); g.arc((sw - vw * k) / 2 + p[0] * k, (sh - vh * k) / 2 + p[1] * k, 4, 0, 7); g.fill(); });
    $('fill').style.width = Math.min(100, 100 * n / WANT) + '%';
    say(r.corners ? 'The board: ' + r.corners + ' corners. Views kept: ' + n + ' of ' + WANT + (r.kept ? '' : '. Move to a new angle.')
                  : 'Views kept: ' + n + ' of ' + WANT + '. Looking for the board...');
    if (n >= WANT) { say('Working it out...'); setTimeout(function () { show(cal.calibrate()); }, 30); }
  }
  // the same camera the games ask for, so the numbers fit
  navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment', width: { ideal: 1280 }, height: { ideal: 720 } }, audio: false }).then(function (s) {
    video.srcObject = s; video.play();
    say('Loading OpenCV...');
    var wait = setInterval(function () {
      if (typeof window.cv === 'undefined') return;
      var go = function (cv) { cal = new window.DioramaCalibrate.Calibrator(cv); say('Show the camera the board.'); requestAnimationFrame(frame); };
      if (window.cv instanceof Promise) { clearInterval(wait); window.cv.then(function (cv) { window.cv = cv; go(cv); }); }
      else if (window.cv.Mat) { clearInterval(wait); go(window.cv); }
    }, 100);
  }).catch(function (e) { say('The camera didn\'t start: ' + (e && (e.message || e.name) || e)); });
})();
