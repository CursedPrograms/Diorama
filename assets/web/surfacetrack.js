// Diorama - a surface (a table, a floor) tracked with any camera: the JavaScript twin of src/surfacetrack.cpp, step for step.
// CLAHE (contrast), ORB features, matching (Lowe's ratio), a RANSAC homography (only what moves as one flat thing),
// then the surface's pose (solvePnP). Its map (as ARCore's): points only ever added (a second view in the same place
// confirms each), so a place seen before is found again; its boundary, the convex hull of them, grows. Needs OpenCV.js.
// Poses: surface -> camera in OpenCV's camera axes (x right, y down, z ahead); R row by row (9), t (3).
(function () {
  'use strict';
  var D2R = Math.PI / 180;
  function mul3(A, B) { var C = []; for (var r = 0; r < 3; ++r) for (var c = 0; c < 3; ++c) C.push(A[r * 3] * B[c] + A[r * 3 + 1] * B[3 + c] + A[r * 3 + 2] * B[6 + c]); return C; }
  function apply3(A, v) { return [A[0] * v[0] + A[1] * v[1] + A[2] * v[2], A[3] * v[0] + A[4] * v[1] + A[5] * v[2], A[6] * v[0] + A[7] * v[1] + A[8] * v[2]]; }
  function transpose3(A) { return [A[0], A[3], A[6], A[1], A[4], A[7], A[2], A[5], A[8]]; }

  // the surface at the lock: the camera at height h looking down by tilt; its origin where the middle of the view meets it
  function lockPose(s) {
    var F = [1, 0, 0, 0, -1, 0, 0, 0, -1];                                                  // three's camera -> OpenCV's
    if (s.facing) return { R: mul3(F, [1, 0, 0, 0, 0, -1, 0, 1, 0]), t: apply3(F, [0, 0, -s.height]) };   // a wall facing the camera
    var th = s.tiltDeg * D2R, h = s.height;
    var Rcw = [1, 0, 0, 0, Math.cos(th), Math.sin(th), 0, -Math.sin(th), Math.cos(th)];   // camera -> world
    var origin = [0, 0, -h / Math.tan(th)], camPos = [0, h, 0];
    var Rwc = transpose3(Rcw);
    var R = Rwc, t = apply3(Rwc, [origin[0] - camPos[0], origin[1] - camPos[1], origin[2] - camPos[2]]);
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
    this.rows = []; this.seen = []; this.cand = { rows: [], X: [], age: [] };   // the map's descriptors (a row each), how often matched; the candidates
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
    var self = this;
    this.rows = rows.map(function (r) { return desc.data.slice(r * cols, (r + 1) * cols); });
    this.seen = rows.map(function () { return 1; });
    this.cand = { rows: [], X: [], age: [] };
    kps.delete(); desc.delete();
    this.refPx = px; this.refX = X;
    this.rebuild();
    void self;
    return this.refCount >= s.minInliers * 2;
  };
  // the map's descriptors as one Mat (for the matcher), after it changes
  Tracker.prototype.rebuild = function () {
    var cv = this.cv, n = this.rows.length, cols = n ? this.rows[0].length : 32, all = new Uint8Array(n * cols);
    for (var i = 0; i < n; ++i) all.set(this.rows[i], i * cols);
    if (this.refDesc) this.refDesc.delete();
    this.refDesc = cv.matFromArray(n, cols, cv.CV_8U, all);
    this.refCount = n;
  };
  // the frame's features the map doesn't have yet: placed on the plane; seen in the same place twice, they join the map
  Tracker.prototype.grow = function (kps, desc, matched, P) {
    var cv = this.cv, s = this.s, K = this.K, fi = 1 / K[0], n = apply3(P.R, [0, 1, 0]), RT = transpose3(P.R), nt = n[0] * P.t[0] + n[1] * P.t[1] + n[2] * P.t[2];
    var cols = desc.cols, newRows = [], newX = [];
    for (var i = 0; i < kps.size(); ++i) {
      if (matched[i]) continue;
      var pt = kps.get(i).pt, d = [(pt.x - K[2]) * fi, (pt.y - K[5]) * fi, 1];
      var den = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
      if (Math.abs(den) < 1e-6) continue;
      var sd = nt / den;
      if (sd <= 0 || sd > 8) continue;                              // behind, or too far to trust
      var Xs = apply3(RT, [d[0] * sd - P.t[0], d[1] * sd - P.t[1], d[2] * sd - P.t[2]]);
      if (Math.hypot(Xs[0], Xs[2]) > 4) continue;                   // a plane is a few metres at most
      newRows.push(desc.data.slice(i * cols, (i + 1) * cols)); newX.push([Xs[0], Xs[2]]);
    }
    if (!newRows.length) return;
    var used = newRows.map(function () { return false; }), C = this.cand, promoted = C.rows.map(function () { return false; }), grew = false;
    if (C.rows.length) {   // the same feature, in the same place on the plane (2 cm, a hair more far off): it joins the map
      var nd = new Uint8Array(newRows.length * cols), cd = new Uint8Array(C.rows.length * cols);
      newRows.forEach(function (r, j) { nd.set(r, j * cols); }); C.rows.forEach(function (r, j) { cd.set(r, j * cols); });
      var Nm = cv.matFromArray(newRows.length, cols, cv.CV_8U, nd), Cm = cv.matFromArray(C.rows.length, cols, cv.CV_8U, cd), knn = new cv.DMatchVectorVector();
      this.matcher.knnMatch(Nm, Cm, knn, 2);
      for (var k = 0; k < knn.size(); ++k) {
        var m = knn.get(k);
        if (m.size() >= 1) {
          var m0 = m.get(0), ok = m0.distance <= 50 && (m.size() < 2 || m0.distance < s.ratio * m.get(1).distance);
          var a = newX[m0.queryIdx], c = C.X[m0.trainIdx];
          if (ok && !promoted[m0.trainIdx] && Math.hypot(a[0] - c[0], a[1] - c[1]) <= 0.02 + 0.01 * Math.hypot(a[0], a[1])) {
            this.rows.push(newRows[m0.queryIdx]); this.refX.push((a[0] + c[0]) / 2, 0, (a[1] + c[1]) / 2); this.seen.push(2);
            used[m0.queryIdx] = true; promoted[m0.trainIdx] = true; grew = true;
          }
        }
        m.delete();
      }
      knn.delete(); Nm.delete(); Cm.delete();
    }
    var keep = { rows: [], X: [], age: [] };   // the candidates: the promoted and the old (20 frames) go; the new ones join
    for (var q = 0; q < C.rows.length; ++q) if (!promoted[q] && C.age[q] < 20) { keep.rows.push(C.rows[q]); keep.X.push(C.X[q]); keep.age.push(C.age[q] + 1); }
    for (var r = 0; r < newRows.length; ++r) if (!used[r] && keep.rows.length < 2500) { keep.rows.push(newRows[r]); keep.X.push(newX[r]); keep.age.push(0); }
    this.cand = keep;
    if (this.rows.length > 5000) {   // the map kept in hand: the points matched least go first
      var self = this, order = this.rows.map(function (_, i) { return i; }).sort(function (x, y) { return self.seen[y] - self.seen[x]; }).slice(0, 4500);
      this.rows = order.map(function (i) { return self.rows[i]; }); this.seen = order.map(function (i) { return self.seen[i]; });
      var X2 = []; order.forEach(function (i) { X2.push(self.refX[i * 3], 0, self.refX[i * 3 + 2]); }); this.refX = X2; grew = true;
    }
    if (grew) this.rebuild();
  };
  // its boundary: the convex hull of its points (seen twice), on the plane: [[x, z], ...]
  Tracker.prototype.boundary = function () {
    var pts = [];
    for (var i = 0; i < this.seen.length; ++i) if (this.seen[i] >= 2) pts.push([this.refX[i * 3], this.refX[i * 3 + 2]]);
    if (pts.length < 3) { pts = []; for (var j = 0; j < this.refX.length; j += 3) pts.push([this.refX[j], this.refX[j + 2]]); }
    if (pts.length < 3) return [];
    pts.sort(function (a, b) { return a[0] - b[0] || a[1] - b[1]; });   // Andrew's monotone chain
    var cross = function (o, a, b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); }, lo = [], hi = [];
    pts.forEach(function (p) { while (lo.length >= 2 && cross(lo[lo.length - 2], lo[lo.length - 1], p) <= 0) lo.pop(); lo.push(p); });
    for (var k = pts.length - 1; k >= 0; --k) { var p = pts[k]; while (hi.length >= 2 && cross(hi[hi.length - 2], hi[hi.length - 1], p) <= 0) hi.pop(); hi.push(p); }
    lo.pop(); hi.pop();
    return lo.concat(hi);
  };
  // enough detail in the middle of the view to find a plane there
  Tracker.prototype.lockable = function (src) {
    var cv = this.cv, gray = this.prepare(src), mask = cv.Mat.zeros(gray.rows, gray.cols, cv.CV_8U);
    var roi = mask.roi(new cv.Rect(gray.cols >> 2, gray.rows >> 2, gray.cols >> 1, gray.rows >> 1)); roi.setTo(new cv.Scalar(255)); roi.delete();
    var kps = new cv.KeyPointVector(); this.orb.detect(gray, kps, mask);
    var n = kps.size(); kps.delete(); mask.delete(); gray.delete();
    return n >= this.s.minInliers * 5;
  };
  // before locking: the features it sees in this frame ({ count, points: [x, y, ...] in the frame's own pixels })
  Tracker.prototype.preview = function (src) {
    var cv = this.cv, gray = this.prepare(src), kps = new cv.KeyPointVector();
    if (!this.noMask) this.noMask = new cv.Mat();
    this.orb.detect(gray, kps, this.noMask);
    var sw = src.videoWidth || src.naturalWidth || src.width, k = sw / this.s.width, pts = [];
    for (var i = 0; i < kps.size(); ++i) { var p = kps.get(i).pt; pts.push(p.x * k, p.y * k); }
    var n = kps.size(); kps.delete(); gray.delete();
    return { count: n, points: pts };
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
    var a = [], b = [], X = [], mi = [], fidx = [];
    for (var i = 0; i < knn.size(); ++i) {
      var m = knn.get(i);
      if (m.size() === 2) {
        var m0 = m.get(0), m1 = m.get(1);
        if (m0.distance < s.ratio * m1.distance) {
          var q = m0.queryIdx, p = kps.get(m0.trainIdx).pt;
          a.push(this.refX[q * 3], this.refX[q * 3 + 2]); b.push(p.x, p.y); X.push(this.refX[q * 3], this.refX[q * 3 + 1], this.refX[q * 3 + 2]);
          mi.push(q); fidx.push(m0.trainIdx);
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
    var pb = [], pX = [], matched = new Array(kps.size()).fill(false);
    if (!H.empty()) for (var j = 0; j < nm; ++j) if (inl.data[j]) { pb.push(b[j * 2], b[j * 2 + 1]); pX.push(X[j * 3], X[j * 3 + 1], X[j * 3 + 2]); matched[fidx[j]] = true; this.seen[mi[j]]++; }
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
      if (this.inliers >= s.minInliers * 2 && (this.growTick = (this.growTick || 0) + 1) % 3 === 0) this.grow(kps, desc, matched, this.last);   // well found: the map grows from this view
    } else this.lastGood = false;
    obj.delete(); img.delete(); Km.delete(); dist.delete(); rvec.delete(); tvec.delete();
    return done(out);
  };
  window.SurfaceTrack = { Tracker: Tracker, lockPose: lockPose };
})();
