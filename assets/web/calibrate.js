// Diorama - camera calibration with the ChArUco board: the JavaScript twin of src/calibrate.cpp, step for step. Needs
// OpenCV.js (window.cv). The board: 5 x 7 squares, markers 0.7 of a square, ArUco 4x4 (ids 0..16).
// addView(frame) keeps the views that differ; calibrate() gives { width, height, K: [fx, fy, cx, cy], dist, rms, views }.
(function () {
  'use strict';
  function Calibrator(cv) {
    this.cv = cv;
    this.board = new cv.aruco_CharucoBoard(new cv.Size(5, 7), 1, 0.7, cv.getPredefinedDictionary(cv.DICT_4X4_50), new cv.Mat());
    this.det = new cv.aruco_CharucoDetector(this.board, new cv.aruco_CharucoParameters(), new cv.aruco_DetectorParameters(), new cv.aruco_RefineParameters(10, 3, true));
    this.obj = []; this.img = []; this.kept = []; this.w = 0; this.h = 0;
    this.canvas = document.createElement('canvas');
    this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
  }
  Calibrator.prototype.views = function () { return this.obj.length; };
  // looks for the board: { corners, kept }
  Calibrator.prototype.addView = function (src) {
    var cv = this.cv;
    var w = src.videoWidth || src.naturalWidth || src.width, h = src.videoHeight || src.naturalHeight || src.height;
    if (!w || !h) return { corners: 0, kept: false };
    if (this.w && (this.w !== w || this.h !== h)) { this.obj = []; this.img = []; this.kept = []; }   // another camera size: start again
    this.w = w; this.h = h;
    if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
    this.ctx.drawImage(src, 0, 0, w, h);
    var rgba = cv.matFromImageData(this.ctx.getImageData(0, 0, w, h)), gray = new cv.Mat();
    cv.cvtColor(rgba, gray, cv.COLOR_RGBA2GRAY); rgba.delete();
    var corners = new cv.Mat(), ids = new cv.Mat(), mc = new cv.MatVector(), mi = new cv.Mat();
    this.det.detectBoard(gray, corners, ids, mc, mi);
    var n = ids.rows, res = { corners: n, kept: false, points: [] };
    for (var j = 0; j < n; ++j) res.points.push([corners.data32F[2 * j], corners.data32F[2 * j + 1]]);
    if (n >= 8) {
      // each ChArUco corner's place on the board (what C++'s matchImagePoints gives: corner k is between the squares,
      // (k % (cols - 1) + 1, floor(k / (cols - 1)) + 1) squares from the top left)
      var op = [], ip = Array.prototype.slice.call(corners.data32F, 0, 2 * n);
      for (var q = 0; q < n; ++q) { var id = ids.data32S[q]; op.push(id % 4 + 1, Math.floor(id / 4) + 1, 0); }
      var o = cv.matFromArray(n, 1, cv.CV_32FC3, op), i = cv.matFromArray(n, 1, cv.CV_32FC2, ip);
      if (o.rows >= 8) {
        var p = i.data32F, m = p.length / 2, cx = 0, cy = 0, spread = 0, k;
        for (k = 0; k < m; ++k) { cx += p[2 * k]; cy += p[2 * k + 1]; }
        cx /= m; cy /= m;
        for (k = 0; k < m; ++k) spread += Math.hypot(p[2 * k] - cx, p[2 * k + 1] - cy);
        spread /= m;
        // a different view from those kept: the board's middle moved by 4% of the picture, or its size changed by 10%
        var far = 0.04 * w, same = this.kept.some(function (q) { return Math.hypot(q[0] - cx, q[1] - cy) < far && Math.abs(q[2] - spread) < 0.1 * spread; });
        if (!same) { this.kept.push([cx, cy, spread]); this.obj.push(o.clone()); this.img.push(i.clone()); res.kept = true; }
      }
      o.delete(); i.delete();
    }
    corners.delete(); ids.delete(); mc.delete(); mi.delete(); gray.delete();
    return res;
  };
  Calibrator.prototype.calibrate = function () {
    var cv = this.cv, r = { ok: false, views: this.obj.length, width: this.w, height: this.h };
    if (this.obj.length < 4) return r;
    var O = new cv.MatVector(), I = new cv.MatVector();
    this.obj.forEach(function (m) { O.push_back(m); }); this.img.forEach(function (m) { I.push_back(m); });
    var K = new cv.Mat(), D = new cv.Mat(), rv = new cv.MatVector(), tv = new cv.MatVector(), s1 = new cv.Mat(), s2 = new cv.Mat(), pe = new cv.Mat();
    try {
      r.rms = cv.calibrateCameraExtended(O, I, new cv.Size(this.w, this.h), K, D, rv, tv, s1, s2, pe, 128 /* CALIB_FIX_K3 (OpenCV.js names the fisheye one, 64) */, new cv.TermCriteria(cv.TermCriteria_COUNT + cv.TermCriteria_EPS, 30, 1e-12));
      var k = K.data64F;
      r.K = [k[0], k[4], k[2], k[5]];
      r.dist = Array.prototype.slice.call(D.data64F, 0, 5);
      r.ok = r.K[0] > 0 && isFinite(r.rms);
      r.fovDeg = 2 * Math.atan(this.w / 2 / r.K[0]) * 180 / Math.PI;
    } catch (e) { r.error = String(e && e.message || e); }
    [O, I, K, D, rv, tv, s1, s2, pe].forEach(function (m) { m.delete(); });
    return r;
  };
  // the board to print (white margin of half a square), as a canvas
  Calibrator.prototype.boardCanvas = function (px) {
    var cv = this.cv, img = new cv.Mat(), c = document.createElement('canvas');
    this.board.generateImage(new cv.Size(6 * px, 8 * px), img, px / 2, 1);
    cv.imshow(c, img); img.delete();
    return c;
  };
  window.DioramaCalibrate = { Calibrator: Calibrator };
})();
