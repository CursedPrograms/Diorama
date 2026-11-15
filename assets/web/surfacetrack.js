// Diorama - a surface (a table, a floor) tracked with any camera: the JavaScript twin of src/surfacetrack.cpp, step for step.
// CLAHE (contrast), ORB features, matching (Lowe's ratio), a RANSAC homography (only what moves as one flat thing),
// then the surface's pose (solvePnP); keyframes as the view moves on. Needs OpenCV.js (window.cv).
// Poses: surface -> camera in OpenCV's camera axes (x right, y down, z ahead); R row by row (9), t (3).
(function () {
  'use strict';
  var D2R = Math.PI / 180;
  function mul3(A, B) { var C = []; for (var r = 0; r < 3; ++r) for (var c = 0; c < 3; ++c) C.push(A[r * 3] * B[c] + A[r * 3 + 1] * B[3 + c] + A[r * 3 + 2] * B[6 + c]); return C; }
  function apply3(A, v) { return [A[0] * v[0] + A[1] * v[1] + A[2] * v[2], A[3] * v[0] + A[4] * v[1] + A[5] * v[2], A[6] * v[0] + A[7] * v[1] + A[8] * v[2]]; }
  function transpose3(A) { return [A[0], A[3], A[6], A[1], A[4], A[7], A[2], A[5], A[8]]; }

  // the surface at the lock: the camera at height h looking down by tilt; its origin where the middle of the view meets it
  function lockPose(s) {
    var th = s.tiltDeg * D2R, h = s.height;
    var Rcw = [1, 0, 0, 0, Math.cos(th), Math.sin(th), 0, -Math.sin(th), Math.cos(th)];   // camera -> world
    var origin = [0, 0, -h / Math.tan(th)], camPos = [0, h, 0];
    var Rwc = transpose3(Rcw);
    var R = Rwc, t = apply3(Rwc, [origin[0] - camPos[0], origin[1] - camPos[1], origin[2] - camPos[2]]);
    var F = [1, 0, 0, 0, -1, 0, 0, 0, -1];                                                  // three's camera -> OpenCV's
    return { R: mul3(F, R), t: apply3(F, t) };
  }

  function Tracker(cv, settings) {
    this.cv = cv;
    var s = { fovDeg: 60, tiltDeg: 50, height: 0.6, width: 320, features: 700, ratio: 0.75, ransacPx: 3, minInliers: 15 };
    for (var k in settings || {}) s[k] = settings[k];
    this.s = s;
    this.orb = new cv.ORB(s.features);
    this.clahe = new cv.CLAHE(2.0, new cv.Size(8, 8));
    this.matcher = new cv.BFMatcher(cv.NORM_HAMMING, false);
    this.canvas = document.createElement('canvas');
    this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
    this.refDesc = null; this.refPx = []; this.refX = []; this.refCount = 0;
    this.last = null; this.lastGood = false; this.inliers = 0;
  }
  Tracker.prototype.locked = function () { return this.refX.length > 0; };
  // a frame (a video, canvas or picture) into a small, evened-out grey picture; K for it
  Tracker.prototype.prepare = function (src) {
    var cv = this.cv, sw = src.videoWidth || src.naturalWidth || src.width, sh = src.videoHeight || src.naturalHeight || src.height;
    var w = this.s.width, h = Math.round(sh * w / sw);
    if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
    this.ctx.drawImage(src, 0, 0, w, h);
    var rgba = cv.matFromImageData(this.ctx.getImageData(0, 0, w, h)), g = new cv.Mat(), gray = new cv.Mat();
    cv.cvtColor(rgba, g, cv.COLOR_RGBA2GRAY);
    this.clahe.apply(g, gray);
    rgba.delete(); g.delete();
    var f = (w / 2) / Math.tan(this.s.fovDeg * 0.5 * D2R);
    this.K = [f, 0, w / 2, 0, f, h / 2, 0, 0, 1];
    return gray;
  };
  // the reference: the features in the middle of this frame, each placed on the surface seen at pose P
  Tracker.prototype.reference = function (gray, P, margin) {
    var cv = this.cv, s = this.s;
    var mask = cv.Mat.zeros(gray.rows, gray.cols, cv.CV_8U);
    var roi = mask.roi(new cv.Rect(Math.round(gray.cols * margin), Math.round(gray.rows * margin), Math.round(gray.cols * (1 - 2 * margin)), Math.round(gray.rows * (1 - 2 * margin))));
    roi.setTo(new cv.Scalar(255)); roi.delete();
    var kps = new cv.KeyPointVector(), desc = new cv.Mat();
    this.orb.detectAndCompute(gray, mask, kps, desc);
    mask.delete();
    if (kps.size() < s.minInliers * 2) { kps.delete(); desc.delete(); return false; }
    var K = this.K, fi = 1 / K[0], n = apply3(P.R, [0, 1, 0]), RT = transpose3(P.R), nt = n[0] * P.t[0] + n[1] * P.t[1] + n[2] * P.t[2];
    var px = [], X = [], rows = [], cols = desc.cols;
    for (var i = 0; i < kps.size(); ++i) {
      var pt = kps.get(i).pt;
      var d = [(pt.x - K[2]) * fi, (pt.y - K[5]) * fi, 1];
      var den = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
      if (Math.abs(den) < 1e-6) continue;
      var sd = nt / den;
      if (sd <= 0) continue;                                   // above the horizon: the ray never meets it
      var Xc = [d[0] * sd - P.t[0], d[1] * sd - P.t[1], d[2] * sd - P.t[2]], Xs = apply3(RT, Xc);
      px.push(pt.x, pt.y); X.push(Xs[0], 0, Xs[2]); rows.push(i);
    }
    var keep = new Uint8Array(rows.length * cols);
    rows.forEach(function (r, j) { keep.set(desc.data.subarray(r * cols, (r + 1) * cols), j * cols); });
    kps.delete(); desc.delete();
    if (this.refDesc) this.refDesc.delete();
    this.refDesc = cv.matFromArray(rows.length, cols, cv.CV_8U, keep);
    this.refPx = px; this.refX = X; this.refCount = rows.length;
    return this.refCount >= s.minInliers * 2;
  };
  Tracker.prototype.lock = function (src) {
    var gray = this.prepare(src), P = lockPose(this.s);
    var ok = this.reference(gray, P, 0.15);
    gray.delete();
    if (!ok) { this.refX = []; return false; }
    this.last = P; this.lastGood = true; this.inliers = this.refCount;
    return true;
  };
  // where the surface is in this frame: { ok, R, t, inliers }
  Tracker.prototype.track = function (src) {
    var cv = this.cv, s = this.s, out = { ok: false, R: this.last && this.last.R, t: this.last && this.last.t, inliers: 0 };
    if (!this.locked()) return out;
    var gray = this.prepare(src), kps = new cv.KeyPointVector(), desc = new cv.Mat();
    if (!this.noMask) this.noMask = new cv.Mat();
    this.orb.detectAndCompute(gray, this.noMask, kps, desc);
    this.inliers = 0;
    var done = function (r) { gray.delete(); kps.delete(); desc.delete(); return r; };
    if (desc.rows === 0) { this.lastGood = false; return done(out); }
    var knn = new cv.DMatchVectorVector();
    this.matcher.knnMatch(this.refDesc, desc, knn, 2);
    var a = [], b = [], X = [];
    for (var i = 0; i < knn.size(); ++i) {
      var m = knn.get(i);
      if (m.size() === 2) {
        var m0 = m.get(0), m1 = m.get(1);
        if (m0.distance < s.ratio * m1.distance) {
          var q = m0.queryIdx, p = kps.get(m0.trainIdx).pt;
          a.push(this.refPx[q * 2], this.refPx[q * 2 + 1]); b.push(p.x, p.y); X.push(this.refX[q * 3], this.refX[q * 3 + 1], this.refX[q * 3 + 2]);
        }
      }
      m.delete();
    }
    knn.delete();
    var nm = a.length / 2;
    if (nm < s.minInliers) { this.lastGood = false; return done(out); }
    // RANSAC: only the matches that move together as one flat surface
    var A = cv.matFromArray(nm, 1, cv.CV_32FC2, a), B = cv.matFromArray(nm, 1, cv.CV_32FC2, b), inl = new cv.Mat();
    var H = cv.findHomography(A, B, cv.RANSAC, s.ransacPx, inl);
    var pb = [], pX = [];
    if (!H.empty()) for (var j = 0; j < nm; ++j) if (inl.data[j]) { pb.push(b[j * 2], b[j * 2 + 1]); pX.push(X[j * 3], X[j * 3 + 1], X[j * 3 + 2]); }
    A.delete(); B.delete(); inl.delete(); H.delete();
    this.inliers = pb.length / 2;
    if (this.inliers < s.minInliers) { this.lastGood = false; return done(out); }
    // the pose: from the last one when it was good (smooth), else afresh (IPPE: made for flat things)
    var obj = cv.matFromArray(this.inliers, 1, cv.CV_32FC3, pX), img = cv.matFromArray(this.inliers, 1, cv.CV_32FC2, pb);
    var Km = cv.matFromArray(3, 3, cv.CV_64F, this.K), dist = new cv.Mat(), rvec = new cv.Mat(), tvec = cv.matFromArray(3, 1, cv.CV_64F, this.last.t);
    var R0 = cv.matFromArray(3, 3, cv.CV_64F, this.last.R); cv.Rodrigues(R0, rvec); R0.delete();
    var ok = this.lastGood ? cv.solvePnP(obj, img, Km, dist, rvec, tvec, true, cv.SOLVEPNP_ITERATIVE)
                           : cv.solvePnP(obj, img, Km, dist, rvec, tvec, false, cv.SOLVEPNP_IPPE);
    if (ok) {
      var Rm = new cv.Mat(); cv.Rodrigues(rvec, Rm);
      out.R = Array.prototype.slice.call(Rm.data64F, 0, 9); out.t = Array.prototype.slice.call(tvec.data64F, 0, 3); Rm.delete();
      out.ok = true; out.inliers = this.inliers;
      this.last = { R: out.R, t: out.t }; this.lastGood = true;
      // a keyframe: the view has moved on and fewer features match; while it's still well found, this frame becomes the reference
      if (this.inliers < Math.max(s.minInliers * 4, Math.floor(this.refCount * 35 / 100))) {
        var keep = { d: this.refDesc.clone(), px: this.refPx, X: this.refX, n: this.refCount };
        if (!this.reference(gray, this.last, 0.05)) { this.refDesc.delete(); this.refDesc = keep.d; this.refPx = keep.px; this.refX = keep.X; this.refCount = keep.n; }
        else keep.d.delete();
      }
    } else this.lastGood = false;
    obj.delete(); img.delete(); Km.delete(); dist.delete(); rvec.delete(); tvec.delete();
    return done(out);
  };
  window.SurfaceTrack = { Tracker: Tracker, lockPose: lockPose };
})();
