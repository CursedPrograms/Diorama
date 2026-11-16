// Diorama - ArUco markers (OpenCV's square markers) found with any camera: the JavaScript twin of src/arucotrack.cpp, step
// for step. Each frame: grey, OpenCV's ArUco detector (its own thresholds, corner refining), then each marker's pose
// (solvePnP, IPPE for squares). Needs OpenCV.js (window.cv).
// Poses: marker -> camera in OpenCV's camera axes (x right, y down, z ahead); the marker's own axes: x right, y up the
// marker, z out of it; its black square is `size` across. R row by row (9), t (3).
(function () {
  'use strict';
  var DICTS = ['DICT_4X4_50', 'DICT_5X5_100', 'DICT_6X6_250', 'DICT_ARUCO_ORIGINAL'];
  function Tracker(cv, settings) {
    this.cv = cv;
    var s = { dict: 0, fovDeg: 60, width: 640, size: 1, K: null, dist: null, calibWidth: 0 };   // K: fx fy cx cy, for frames calibWidth across
    for (var k in settings || {}) s[k] = settings[k];
    this.s = s;
    var dict = cv.getPredefinedDictionary(cv[DICTS[Math.max(0, Math.min(3, s.dict))]]);
    this.det = new cv.aruco_ArucoDetector(dict, new cv.aruco_DetectorParameters(), new cv.aruco_RefineParameters(10, 3, true));
    this.canvas = document.createElement('canvas');
    this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
    var h = s.size / 2;
    this.obj = cv.matFromArray(4, 1, cv.CV_32FC3, [-h, h, 0, h, h, 0, h, -h, 0, -h, -h, 0]);   // the corners' order: OpenCV's (top left, clockwise)
    this.method = cv.SOLVEPNP_IPPE_SQUARE !== undefined ? cv.SOLVEPNP_IPPE_SQUARE : cv.SOLVEPNP_IPPE;
  }
  // the markers in this frame: [{ id, R, t, corners: [x, y] x 4 in the frame's own pixels }]
  Tracker.prototype.detect = function (src) {
    var cv = this.cv, s = this.s;
    var sw = src.videoWidth || src.naturalWidth || src.width, sh = src.videoHeight || src.naturalHeight || src.height;
    if (!sw || !sh) return [];
    var w = Math.min(s.width, sw), h = Math.round(sh * w / sw), k = sw / w;
    if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
    this.ctx.drawImage(src, 0, 0, w, h);
    var rgba = cv.matFromImageData(this.ctx.getImageData(0, 0, w, h)), gray = new cv.Mat();
    cv.cvtColor(rgba, gray, cv.COLOR_RGBA2GRAY); rgba.delete();
    // the lens: from a calibration (for the frame's own size), else from the field of view
    var K;
    if (s.K) { var c = (s.calibWidth || sw) / w; K = [s.K[0] / c, 0, s.K[2] / c, 0, s.K[1] / c, s.K[3] / c, 0, 0, 1]; }
    else { var f = (w / 2) / Math.tan(s.fovDeg * Math.PI / 360); K = [f, 0, w / 2, 0, f, h / 2, 0, 0, 1]; }
    var Km = cv.matFromArray(3, 3, cv.CV_64F, K), dist = s.dist ? cv.matFromArray(1, s.dist.length, cv.CV_64F, s.dist) : new cv.Mat();
    var corners = new cv.MatVector(), ids = new cv.Mat(), rejected = new cv.MatVector();
    this.det.detectMarkers(gray, corners, ids, rejected);
    var out = [];
    for (var i = 0; i < ids.rows; ++i) {
      var c = corners.get(i), px = Array.prototype.slice.call(c.data32F, 0, 8);
      var img = cv.matFromArray(4, 1, cv.CV_32FC2, px), rvec = new cv.Mat(), tvec = new cv.Mat();
      if (cv.solvePnP(this.obj, img, Km, dist, rvec, tvec, false, this.method)) {
        var Rm = new cv.Mat(); cv.Rodrigues(rvec, Rm);
        out.push({ id: ids.data32S[i], R: Array.prototype.slice.call(Rm.data64F, 0, 9), t: Array.prototype.slice.call(tvec.data64F, 0, 3),
                   corners: px.map(function (v) { return v * k; }) });
        Rm.delete();
      }
      img.delete(); rvec.delete(); tvec.delete(); c.delete();
    }
    corners.delete(); ids.delete(); rejected.delete(); gray.delete(); Km.delete(); dist.delete();
    return out;
  };
  window.ArucoTrack = { Tracker: Tracker };
})();
