// Diorama - physics in the web game: Jolt Physics as WASM (JoltPhysics.js, MIT), under the Rigidbody and Collider
// components. The twin of src/physics.cpp (the editor's Simulate and the native player), step for step: keep the two
// in step, so a game does the same everywhere (tools/physicstest compares them).
//
// Each anchor is its own space (things under one marker only meet each other), gravity along the project's
// Physics > Gravity, the anchor's floor at y = 0 when it has one. step() matches bodies to the objects, steps at a fixed
// 60 Hz and writes where the moving ones got to back into their data (position, rotation), as a script would.
// No three.js here: plain numbers, so node can run it too.
(function (G) {
  'use strict';
  var STEP = 1 / 60, MAX_STEPS = 4, D2R = Math.PI / 180, FLOOR = 'floor';
  var KINDS = ['Empty', 'Plane', 'Cube', 'Sphere', 'Capsule', 'Cylinder', 'Quad', 'Picture', 'Model', 'Sprites', 'Light rays', 'Words'];
  var COL_BOX = 0, COL_SPHERE = 1, COL_CAPSULE = 2, COL_MESH = 3;

  // ---- maths: quaternions [x, y, z, w], matrices column-major [16]
  function qAxis(ax, ay, az, a) { var s = Math.sin(a / 2); return [ax * s, ay * s, az * s, Math.cos(a / 2)]; }
  function qMul(a, b) {
    return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
  }
  function qNorm(q) { var l = Math.hypot(q[0], q[1], q[2], q[3]) || 1; return [q[0] / l, q[1] / l, q[2] / l, q[3] / l]; }
  function quatOf(r) {   // Ry * Rx * Rz (three.js's 'YXZ')
    return qMul(qMul(qAxis(0, 1, 0, r[1] * D2R), qAxis(1, 0, 0, r[0] * D2R)), qAxis(0, 0, 1, r[2] * D2R));
  }
  function rotOf(q) {   // a rotation matrix (column-major)
    var x = q[0], y = q[1], z = q[2], w = q[3];
    return [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
            2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
            2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0, 0, 0, 0, 1];
  }
  function eulerOf(q) {   // back to degrees, as three.js's Euler.setFromQuaternion(q, 'YXZ')
    var m = rotOf(qNorm(q)), m13 = m[8], m21 = m[1], m22 = m[5], m23 = m[9], m31 = m[2], m11 = m[0], m33 = m[10];
    var x = Math.asin(-Math.max(-1, Math.min(1, m23))), y, z;
    if (Math.abs(m23) < 0.9999999) { y = Math.atan2(m13, m33); z = Math.atan2(m21, m22); } else { y = Math.atan2(-m31, m11); z = 0; }
    return [x / D2R, y / D2R, z / D2R];
  }
  function mMul(a, b) {
    var r = new Array(16);
    for (var c = 0; c < 4; ++c) for (var rr = 0; rr < 4; ++rr) {
      var s = 0; for (var k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s;
    }
    return r;
  }
  function mInv(m) {   // a general 4x4 inverse
    var a = m, inv = new Array(16);
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    var det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    det = det ? 1 / det : 0;
    for (var i = 0; i < 16; ++i) inv[i] *= det;
    return inv;
  }
  function mPoint(m, p) { return [m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]]; }
  function trs(p, q, s) {
    var m = rotOf(q);
    for (var k = 0; k < 3; ++k) { m[k * 4] *= s[k]; m[k * 4 + 1] *= s[k]; m[k * 4 + 2] *= s[k]; }
    m[12] = p[0]; m[13] = p[1]; m[14] = p[2];
    return m;
  }
  function quatFromRot(m) {   // three.js's Quaternion.setFromRotationMatrix
    var m11 = m[0], m12 = m[4], m13 = m[8], m21 = m[1], m22 = m[5], m23 = m[9], m31 = m[2], m32 = m[6], m33 = m[10], tr = m11 + m22 + m33, s;
    if (tr > 0) { s = 0.5 / Math.sqrt(tr + 1); return [(m32 - m23) * s, (m13 - m31) * s, (m21 - m12) * s, 0.25 / s]; }
    if (m11 > m22 && m11 > m33) { s = 2 * Math.sqrt(1 + m11 - m22 - m33); return [0.25 * s, (m12 + m21) / s, (m13 + m31) / s, (m32 - m23) / s]; }
    if (m22 > m33) { s = 2 * Math.sqrt(1 + m22 - m11 - m33); return [(m12 + m21) / s, 0.25 * s, (m23 + m32) / s, (m13 - m31) / s]; }
    s = 2 * Math.sqrt(1 + m33 - m11 - m22); return [(m13 + m31) / s, (m23 + m32) / s, 0.25 * s, (m21 - m12) / s];
  }
  function poseOf(m) {   // position, rotation and (lossy) scale of a world matrix
    var s = [Math.hypot(m[0], m[1], m[2]), Math.hypot(m[4], m[5], m[6]), Math.hypot(m[8], m[9], m[10])];
    var n = m.slice();
    for (var k = 0; k < 3; ++k) { var l = s[k] > 1e-9 ? s[k] : 1; n[k * 4] /= l; n[k * 4 + 1] /= l; n[k * 4 + 2] /= l; }
    return { p: [m[12], m[13], m[14]], q: qNorm(quatFromRot(n)), s: s };
  }
  // an object's own transform, with Animate added on (as it's drawn), dynamic bodies without (physics moves them)
  function localOf(o, t, withAnim) {
    var pos = o.position.slice(), rot = o.rotation.slice();
    if (withAnim && o.hasAnim !== false && o.animOn !== false) {
      var ph = t * o.animSpeed * Math.PI * 2;
      if (o.anim === 'Float') pos[1] += Math.sin(ph) * o.animAmount;
      else if (o.anim === 'Spin') rot[1] += t * o.animSpeed * 360;
      else if (o.anim === 'Sway') rot[2] += Math.sin(ph) * o.animAmount * 90;
    }
    return trs(pos, quatOf(rot), o.scale);
  }

  // ---- the collider's points (a mesh collider's hull), as physics.cpp makes them
  function pointsOf(o, modelPoints) {
    var v = [];
    var box = function (x0, y0, z0, x1, y1, z1) { for (var i = 0; i < 8; ++i) v.push([i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0]); };
    var round = function (r, y0, y1, caps) {
      var k, j;
      for (k = 0; k < 16; ++k) { var a = k * 2 * Math.PI / 16, x = Math.cos(a) * r, z = Math.sin(a) * r; v.push([x, y0, z]); v.push([x, y1, z]); }
      if (caps) for (j = 1; j <= 3; ++j) for (k = 0; k < 16; ++k) {
        var e = j * (Math.PI / 2) / 4, a2 = k * 2 * Math.PI / 16, rr = Math.cos(e) * r, h = Math.sin(e) * r;
        v.push([Math.cos(a2) * rr, y1 + h, Math.sin(a2) * rr]); v.push([Math.cos(a2) * rr, y0 - h, Math.sin(a2) * rr]);
      }
      if (caps) { v.push([0, y1 + r, 0]); v.push([0, y0 - r, 0]); }
    };
    switch (o.kind) {
    case 'Sphere':
      for (var j = 0; j <= 8; ++j) for (var k = 0; k < 16; ++k) {
        var e = -Math.PI / 2 + j * Math.PI / 8, a = k * 2 * Math.PI / 16;
        v.push([Math.cos(e) * Math.cos(a) * 0.5, Math.sin(e) * 0.5, Math.cos(e) * Math.sin(a) * 0.5]);
      }
      break;
    case 'Cylinder': round(0.5, -1, 1, false); break;
    case 'Capsule': round(0.5, -0.5, 0.5, true); break;
    case 'Plane': box(-5, 0, -5, 5, 0, 5); break;
    case 'Quad': case 'Picture': case 'Words': box(-0.5, -0.5, 0, 0.5, 0.5, 0); break;
    case 'Model':
      var mp = modelPoints ? modelPoints(o) : null;   // [x, y, z, ...] in the model's space (null: not loaded yet)
      if (mp) { var n = mp.length / 3, every = Math.max(1, Math.floor(n / 3000)); for (var i = 0; i < n; i += every) v.push([mp[i * 3], mp[i * 3 + 1], mp[i * 3 + 2]]); }
      break;
    default: box(-0.5, -0.5, -0.5, 0.5, 0.5, 0.5); break;
    }
    if (v.length < 4) { v = []; box(-0.5, -0.5, -0.5, 0.5, 0.5, 0.5); }
    return v;
  }

  function create(Jolt, proj, opts) {
    opts = opts || {};
    var markers = proj.markers || [], anchors = Math.max(1, markers.length), layers = anchors * 2;
    var layerOf = function (a, moving) { return a * 2 + (moving ? 1 : 0); };
    var settings = new Jolt.JoltSettings();
    settings.mMaxBodies = 4096; settings.mMaxBodyPairs = 8192; settings.mMaxContactConstraints = 8192;
    var pairs = new Jolt.ObjectLayerPairFilterTable(layers), bp = new Jolt.BroadPhaseLayerInterfaceTable(layers, 2);
    var bpStatic = new Jolt.BroadPhaseLayer(0), bpMoving = new Jolt.BroadPhaseLayer(1);
    for (var a = 0; a < anchors; ++a) {
      bp.MapObjectToBroadPhaseLayer(layerOf(a, false), bpStatic); bp.MapObjectToBroadPhaseLayer(layerOf(a, true), bpMoving);
      pairs.EnableCollision(layerOf(a, true), layerOf(a, false)); pairs.EnableCollision(layerOf(a, true), layerOf(a, true));
    }
    settings.mObjectLayerPairFilter = pairs; settings.mBroadPhaseLayerInterface = bp;
    settings.mObjectVsBroadPhaseLayerFilter = new Jolt.ObjectVsBroadPhaseLayerFilterTable(bp, 2, pairs, layers);
    var jolt = new Jolt.JoltInterface(settings);
    Jolt.destroy(settings);
    var sys = jolt.GetPhysicsSystem(), bi = sys.GetBodyInterface();
    var V = function (x, y, z) { return new Jolt.Vec3(x, y, z); }, RV = function (x, y, z) { return new Jolt.RVec3(x, y, z); };
    var Q = function (q) { return new Jolt.Quat(q[0], q[1], q[2], q[3]); };
    var free = function () { for (var i = 0; i < arguments.length; ++i) Jolt.destroy(arguments[i]); };
    var W = { bodies: {}, byBody: {}, floors: [], touching: {}, events: [], contacts: [], acc: 0, gravity: [0, -9.81, 0] };
    var keyOf = function (body) { return body.GetID().GetIndexAndSequenceNumber(); };

    var listener = new Jolt.ContactListenerJS();
    listener.OnContactValidate = function () { return Jolt.ValidateResult_AcceptAllContactsForThisBodyPair; };
    listener.OnContactAdded = function (p1, p2, pm) {
      var b1 = Jolt.wrapPointer(p1, Jolt.Body), b2 = Jolt.wrapPointer(p2, Jolt.Body), m = Jolt.wrapPointer(pm, Jolt.ContactManifold);
      var pt = m.GetWorldSpaceContactPointOn1(0), n = m.mWorldSpaceNormal, v1 = b1.GetLinearVelocity(), v1x = v1.GetX(), v1y = v1.GetY(), v1z = v1.GetZ();
      var v2 = b2.GetLinearVelocity();
      W.events.push({ add: true, a: W.byBody[keyOf(b1)], b: W.byBody[keyOf(b2)], trigger: b1.IsSensor() || b2.IsSensor(),
        point: [pt.GetX(), pt.GetY(), pt.GetZ()], normal: [n.GetX(), n.GetY(), n.GetZ()],
        speed: Math.hypot(v2.GetX() - v1x, v2.GetY() - v1y, v2.GetZ() - v1z) });
    };
    listener.OnContactPersisted = function () {};
    listener.OnContactRemoved = function (pp) {
      var pair = Jolt.wrapPointer(pp, Jolt.SubShapeIDPair);
      W.events.push({ add: false, a: pair.GetBody1ID().GetIndexAndSequenceNumber(), b: pair.GetBody2ID().GetIndexAndSequenceNumber() });
    };
    sys.SetContactListener(listener);
    W.gravity = (proj.gravity || [0, -9.81, 0]).slice();
    var gv = V(W.gravity[0], W.gravity[1], W.gravity[2]); sys.SetGravity(gv); free(gv);

    markers.forEach(function (m, a) {   // the paper, table or floor: a big slab under y = 0
      if (m.physicsFloor === false || proj.tracking === 'faces') { W.floors.push(null); return; }
      var he = V(100, 0.5, 100), shape = new Jolt.BoxShape(he, 0.05, null), pos = RV(0, -0.5, 0), rot = new Jolt.Quat(0, 0, 0, 1);
      var bs = new Jolt.BodyCreationSettings(shape, pos, rot, Jolt.EMotionType_Static, layerOf(a, false));
      bs.mFriction = 0.6;
      var body = bi.CreateBody(bs); bi.AddBody(body.GetID(), Jolt.EActivation_DontActivate);
      free(bs, he, pos, rot);
      W.floors.push(body); W.byBody[keyOf(body)] = FLOOR;
    });

    var shapeOf = function (o, s) {
      s = [Math.abs(s[0]), Math.abs(s[1]), Math.abs(s[2])];
      var settingsOf = function () {   // shape settings (then a centre offset round them)
        var shape = o.colShape | 0;
        if (shape === COL_SPHERE) return new Jolt.SphereShapeSettings(Math.max(0.0005, o.colRadius * Math.max(s[0], s[1], s[2])), null);
        if (shape === COL_CAPSULE) {
          var r = Math.max(0.0005, o.colRadius * Math.max(s[0], s[2])), half = o.colHeight * s[1] / 2 - r;
          return half > 1e-4 ? new Jolt.CapsuleShapeSettings(half, r, null) : new Jolt.SphereShapeSettings(r, null);
        }
        var boxOf = function (h) {
          h = [Math.max(h[0], 0.0005), Math.max(h[1], 0.0005), Math.max(h[2], 0.0005)];
          var hv = V(h[0], h[1], h[2]), b = new Jolt.BoxShapeSettings(hv, Math.min(0.05, Math.min(h[0], h[1], h[2]) * 0.5), null); free(hv); return b;
        };
        if (shape === COL_MESH) {
          var pts = pointsOf(o, opts.modelPoints).map(function (p) { return [p[0] * s[0], p[1] * s[1], p[2] * s[2]]; });
          var lo = [1e30, 1e30, 1e30], hi = [-1e30, -1e30, -1e30];
          pts.forEach(function (p) { for (var k = 0; k < 3; ++k) { lo[k] = Math.min(lo[k], p[k]); hi[k] = Math.max(hi[k], p[k]); } });
          var thin = Math.min(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
          if (thin > 0.002) {
            var hs = new Jolt.ConvexHullShapeSettings();
            pts.forEach(function (p) { var pv = V(p[0], p[1], p[2]); hs.mPoints.push_back(pv); free(pv); });
            hs.mMaxConvexRadius = Math.min(0.05, thin * 0.1);
            return { mesh: hs };
          }
          var c = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
          return { offset: c, inner: boxOf([(hi[0] - lo[0]) / 2, (hi[1] - lo[1]) / 2, (hi[2] - lo[2]) / 2]) };
        }
        return boxOf([o.colSize[0] * s[0] / 2, o.colSize[1] * s[1] / 2, o.colSize[2] * s[2] / 2]);
      };
      var st = settingsOf(), inner = st, offset = null;
      if (st.mesh) {
        var res = st.mesh.Create();
        if (res.IsValid()) { var hull = res.Get(); hull.AddRef(); Jolt.destroy(st.mesh); return hull; }
        Jolt.destroy(st.mesh);
        var hv0 = V(0.5 * s[0], 0.5 * s[1], 0.5 * s[2]); inner = new Jolt.BoxShapeSettings(hv0, 0.01, null); free(hv0);
      } else if (st.inner) { inner = st.inner; offset = st.offset; }
      else if ((o.colShape | 0) !== COL_MESH) {
        var c2 = [o.colCentre[0] * s[0], o.colCentre[1] * s[1], o.colCentre[2] * s[2]];
        if (c2[0] * c2[0] + c2[1] * c2[1] + c2[2] * c2[2] > 1e-12) offset = c2;
      }
      var made = inner;
      if (offset) { var ov = V(offset[0], offset[1], offset[2]), oq = new Jolt.Quat(0, 0, 0, 1); made = new Jolt.RotatedTranslatedShapeSettings(ov, oq, inner); free(ov, oq); }
      var r2 = made.Create(), shapeOut = r2.Get(); shapeOut.AddRef();
      Jolt.destroy(made);
      return shapeOut;
    };
    var signature = function (o, s) {
      var mesh = (o.colShape | 0) === COL_MESH;
      return [o.colShape | 0].concat(o.colSize, o.colCentre, [o.colRadius, o.colHeight, !!o.isTrigger, o.friction, o.bounciness, !!o.hasBody, o.mass, o.drag,
        o.angularDrag, o.useGravity !== false, !!o.isKinematic, o.freeze | 0, mesh ? o.kind : 0, mesh ? (o.file || '') + (opts.modelPoints && opts.modelPoints(o) ? '+' : '') : '',
        Math.round(s[0] * 1e4), Math.round(s[1] * 1e4), Math.round(s[2] * 1e4)]).join(' ');
    };
    var removeBody = function (id) {
      var b = W.bodies[id];
      Object.keys(W.touching).forEach(function (k) {   // what it touched hears it leave
        var t = W.touching[k];
        if (t.a === id || t.b === id) { W.contacts.push({ a: t.a, b: t.b, enter: false, trigger: t.trigger }); delete W.touching[k]; }
      });
      delete W.byBody[b.key];
      bi.RemoveBody(b.body.GetID()); bi.DestroyBody(b.body.GetID());
      delete W.bodies[id];
    };
    var dynamicOf = function (o) { return !!o.hasBody && !o.isKinematic && (o.freeze | 0) !== 63; };

    var world = {
      // lists[a]: anchor a's objects, parents before their children: [{ o, on }] (on: it and its parents are shown)
      step: function (dt, t, lists) {
        var g = proj.gravity || [0, -9.81, 0];
        if (g[0] !== W.gravity[0] || g[1] !== W.gravity[1] || g[2] !== W.gravity[2]) { W.gravity = g.slice(); var gv2 = V(g[0], g[1], g[2]); sys.SetGravity(gv2); free(gv2); }
        Object.keys(W.bodies).forEach(function (k) { W.bodies[k].seen = false; });
        // 1. the bodies matched to the objects
        var items = [];
        for (var a = 0; a < lists.length && a < anchors; ++a) {
          var worldOf = {};
          lists[a].forEach(function (e) {
            var o = e.o, parent = null, on = e.on !== false && o.active !== false;
            if (o.parent !== -1) { var pw = worldOf[o.parent]; if (!pw) on = false; else { parent = pw.m; on = on && pw.on; } }
            var local = localOf(o, t, !dynamicOf(o)), wm = parent ? mMul(parent, local) : local;
            worldOf[o.id] = { m: wm, on: on };
            if (on && o.hasCollider && o.colliderOn !== false) items.push({ anchor: a, o: o, world: wm, parent: parent });
          });
        }
        items.forEach(function (it) {
          var o = it.o, pose = poseOf(it.world), sig = signature(o, pose.s), b = W.bodies[o.id], keepV = null, keepW = null;
          if (b && (b.sig !== sig || b.anchor !== it.anchor)) {
            var lv = bi.GetLinearVelocity(b.body.GetID()), av = bi.GetAngularVelocity(b.body.GetID());
            keepV = [lv.GetX(), lv.GetY(), lv.GetZ()]; keepW = [av.GetX(), av.GetY(), av.GetZ()];
            removeBody(o.id); b = null;
          }
          var dyn = dynamicOf(o);
          if (!b) {
            var shape = shapeOf(o, pose.s), pos = RV(pose.p[0], pose.p[1], pose.p[2]), rot = Q(pose.q);
            var bs = new Jolt.BodyCreationSettings(shape, pos, rot, dyn ? Jolt.EMotionType_Dynamic : Jolt.EMotionType_Kinematic, layerOf(it.anchor, true));
            bs.mIsSensor = !!o.isTrigger;
            bs.mFriction = Math.max(0, o.friction); bs.mRestitution = Math.min(1, Math.max(0, o.bounciness));
            bs.mLinearDamping = Math.max(0, o.drag); bs.mAngularDamping = Math.max(0, o.angularDrag);
            bs.mGravityFactor = o.hasBody && o.useGravity !== false ? 1 : 0;
            bs.mOverrideMassProperties = Jolt.EOverrideMassProperties_CalculateInertia;
            bs.mMassPropertiesOverride.mMass = Math.max(0.001, o.hasBody ? o.mass : 1);
            if (dyn) { bs.mMotionQuality = Jolt.EMotionQuality_LinearCast; bs.mAllowedDOFs = 63 & ~(o.freeze | 0); }
            if (keepV) { var kv = V(keepV[0], keepV[1], keepV[2]), kw = V(keepW[0], keepW[1], keepW[2]); bs.mLinearVelocity = kv; bs.mAngularVelocity = kw; free(kv, kw); }
            var body = bi.CreateBody(bs);
            free(bs, pos, rot); shape.Release();
            if (!body || !Jolt.getPointer(body)) return;   // the world is full
            bi.AddBody(body.GetID(), Jolt.EActivation_Activate);
            b = { body: body, key: keyOf(body), anchor: it.anchor, object: o.id, dynamic: dyn, sig: sig,
                  wrotePos: o.position.slice(), wroteRot: o.rotation.slice(), target: pose.p, targetQ: pose.q };
            W.bodies[o.id] = b; W.byBody[b.key] = o.id;
          } else if (dyn) {   // a script (or the Inspector) put it somewhere: it goes there
            if (String(b.wrotePos) !== String(o.position) || String(b.wroteRot) !== String(o.rotation)) {
              var p2 = RV(pose.p[0], pose.p[1], pose.p[2]), q2 = Q(pose.q);
              bi.SetPositionAndRotation(b.body.GetID(), p2, q2, Jolt.EActivation_Activate); free(p2, q2);
              b.wrotePos = o.position.slice(); b.wroteRot = o.rotation.slice();
            }
          } else { b.target = pose.p; b.targetQ = pose.q; }
          b.seen = true;
        });
        Object.keys(W.bodies).forEach(function (k) { if (!W.bodies[k].seen) removeBody(W.bodies[k].object); });

        // 2. the world, at 60 Hz
        W.acc = Math.min(W.acc + Math.max(0, dt), STEP * MAX_STEPS);
        while (W.acc >= STEP) {
          W.acc -= STEP;
          Object.keys(W.bodies).forEach(function (k) {
            var b = W.bodies[k]; if (b.dynamic) return;
            var id = b.body.GetID(), cp = bi.GetPosition(id), cx = cp.GetX(), cy = cp.GetY(), cz = cp.GetZ(), cq = bi.GetRotation(id);
            var dp = (cx - b.target[0]) * (cx - b.target[0]) + (cy - b.target[1]) * (cy - b.target[1]) + (cz - b.target[2]) * (cz - b.target[2]);
            var dq = Math.abs(cq.GetX() * b.targetQ[0] + cq.GetY() * b.targetQ[1] + cq.GetZ() * b.targetQ[2] + cq.GetW() * b.targetQ[3]);
            if (dp > 1e-12 || dq < 1 - 1e-6) { var tp = RV(b.target[0], b.target[1], b.target[2]), tq = Q(b.targetQ); bi.MoveKinematic(id, tp, tq, STEP); free(tp, tq); }
            else if (bi.IsActive(id)) { var z = V(0, 0, 0); bi.SetLinearVelocity(id, z); bi.SetAngularVelocity(id, z); free(z); }
          });
          jolt.Step(STEP, 1);
        }

        // 3. where the moving ones got to, into their data (in their parent's space)
        items.forEach(function (it) {
          var b = W.bodies[it.o.id]; if (!b || !b.dynamic) return;
          var id = b.body.GetID(), cp = bi.GetPosition(id), p = [cp.GetX(), cp.GetY(), cp.GetZ()], cq = bi.GetRotation(id), q = [cq.GetX(), cq.GetY(), cq.GetZ(), cq.GetW()];
          if (it.parent) {
            p = mPoint(mInv(it.parent), p);
            var pq = poseOf(it.parent).q;
            q = qMul([-pq[0], -pq[1], -pq[2], pq[3]], q);
          }
          var e = eulerOf(q), o = it.o;
          o.position[0] = p[0]; o.position[1] = p[1]; o.position[2] = p[2];
          o.rotation[0] = e[0]; o.rotation[1] = e[1]; o.rotation[2] = e[2];
          b.wrotePos = o.position.slice(); b.wroteRot = o.rotation.slice();
        });

        // 4. contacts that began or ended: each pair once
        W.events.forEach(function (e) {
          var a = e.a, b = e.b;
          if (!e.add) { a = W.byBody[e.a]; b = W.byBody[e.b]; if (a === undefined || b === undefined) return; }
          if (a === undefined || b === undefined) return;
          if (a === FLOOR) { var sw = a; a = b; b = sw; }
          if (a === FLOOR) return;
          var first = a, second = b;
          if (b !== FLOOR && b < a) { first = b; second = a; }
          var key = first + '|' + second, tc = W.touching[key];
          if (e.add) {
            if (!tc) {
              tc = W.touching[key] = { a: first, b: second === FLOOR ? -1 : second, n: 0, trigger: e.trigger };
              // Jolt's normal points from body 1 to body 2; first is pushed the other way when it's body 1
              var n = first === e.a ? [-e.normal[0], -e.normal[1], -e.normal[2]] : e.normal;
              W.contacts.push({ a: tc.a, b: tc.b, enter: true, trigger: e.trigger, point: e.point, normal: n, speed: e.speed });
            }
            tc.n++;
          } else if (tc && --tc.n <= 0) {
            W.contacts.push({ a: tc.a, b: tc.b, enter: false, trigger: tc.trigger });
            delete W.touching[key];
          }
        });
        W.events = [];
      },
      takeContacts: function () { var c = W.contacts; W.contacts = []; return c; },
      velocity: function (id, angular) {
        var b = W.bodies[id]; if (!b) return null;
        var v = angular ? bi.GetAngularVelocity(b.body.GetID()) : bi.GetLinearVelocity(b.body.GetID());
        return [v.GetX(), v.GetY(), v.GetZ()];
      },
      setVelocity: function (id, angular, v) {
        var b = W.bodies[id]; if (!b || !b.dynamic) return false;
        var jv = V(v[0], v[1], v[2]);
        if (angular) bi.SetAngularVelocity(b.body.GetID(), jv); else bi.SetLinearVelocity(b.body.GetID(), jv);
        free(jv); bi.ActivateBody(b.body.GetID());
        return true;
      },
      // mode: 0 force, 1 impulse, 2 acceleration, 3 velocity change (Unity's ForceMode)
      addForce: function (id, f, mode, torque) {
        var b = W.bodies[id]; if (!b || !b.dynamic) return false;
        var body = b.body, v = V(f[0], f[1], f[2]);
        if (!torque) {
          var mass = 1 / Math.max(1e-9, body.GetMotionProperties().GetInverseMass());
          if (mode === 1) body.AddImpulse(v);
          else if (mode === 2) { var fm = V(f[0] * mass, f[1] * mass, f[2] * mass); body.AddForce(fm); free(fm); }
          else if (mode === 3) { var lv = body.GetLinearVelocity(), nv = V(lv.GetX() + f[0], lv.GetY() + f[1], lv.GetZ() + f[2]); body.SetLinearVelocity(nv); free(nv); }
          else body.AddForce(v);
        } else {
          if (mode === 1) body.AddAngularImpulse(v);
          else if (mode === 2) { var I = body.GetInverseInertia().Inversed3x3(), tv = I.Multiply3x3(v); body.AddTorque(tv); }
          else if (mode === 3) { var av = body.GetAngularVelocity(), na = V(av.GetX() + f[0], av.GetY() + f[1], av.GetZ() + f[2]); body.SetAngularVelocity(na); free(na); }
          else body.AddTorque(v);
        }
        free(v); bi.ActivateBody(body.GetID());
        return true;
      },
      sleeping: function (id) { var b = W.bodies[id]; return !!b && !bi.IsActive(b.body.GetID()); },
      wake: function (id, on) { var b = W.bodies[id]; if (!b) return false; if (on) bi.ActivateBody(b.body.GetID()); else bi.DeactivateBody(b.body.GetID()); return true; },
      // a ray in anchor's space (from, direction, how far): { id (-1 the floor), floor, point, normal, distance } or null; triggers left out
      raycast: function (anchor, from, dir, maxDistance) {
        var l = Math.hypot(dir[0], dir[1], dir[2]); if (l < 1e-6 || !(maxDistance > 0)) return null;
        var len = Math.min(maxDistance, 1e4), d = [dir[0] / l * len, dir[1] / l * len, dir[2] / l * len];
        var o = RV(from[0], from[1], from[2]), dv = V(d[0], d[1], d[2]), ray = new Jolt.RRayCast(o, dv), rs = new Jolt.RayCastSettings();
        rs.mBackFaceModeConvex = Jolt.EBackFaceMode_IgnoreBackFaces;
        var col = new Jolt.CastRayAllHitCollisionCollector();
        var f1 = new Jolt.BroadPhaseLayerFilter(), f2 = new Jolt.ObjectLayerFilter(), f3 = new Jolt.BodyFilter(), f4 = new Jolt.ShapeFilter();
        sys.GetNarrowPhaseQuery().CastRay(ray, rs, col, f1, f2, f3, f4);
        col.Sort();
        var hit = null, lock = sys.GetBodyLockInterfaceNoLock();
        for (var i = 0; i < col.mHits.size() && !hit; ++i) {
          var r = col.mHits.at(i), body = lock.TryGetBody(r.mBodyID);
          if (!body || body.IsSensor() || Math.floor(body.GetObjectLayer() / 2) !== anchor) continue;
          var at = ray.GetPointOnRay(r.mFraction), ax = at.GetX(), ay = at.GetY(), az = at.GetZ();
          var n = body.GetWorldSpaceSurfaceNormal(r.mSubShapeID2, at);
          var who = W.byBody[keyOf(body)];
          hit = { id: who === FLOOR ? -1 : who, floor: who === FLOOR, point: [ax, ay, az], normal: [n.GetX(), n.GetY(), n.GetZ()], distance: r.mFraction * len };
        }
        free(o, dv, ray, rs, col, f1, f2, f3, f4);
        return hit;
      },
      bodies: function () { return Object.keys(W.bodies).length; }
    };
    return world;
  }

  var api = { create: create, any: function (proj) {
    return (proj.markers || []).some(function (m) { return (m.objects || []).some(function (o) { return o.hasCollider; }); });
  }, eulerOf: eulerOf, quatOf: quatOf };
  G.DioramaPhysics = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
