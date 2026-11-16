// Diorama's native player: the game's scripts, run as the web game runs them (assets/web/diorama.js: GameObject,
// getComponent, find, spawn, destroy, the hooks, the errors). The scene itself lives in C++; __dio reads and changes it:
//   get(id, key) / set(id, key, v) / getv(id, key, i) / setv(id, key, i, v)   an object's numbers, words and ticks
//   children(id) / roots() / parent(id) / descendants(id)                     the tree (anchors' roots: negative ids)
//   scripts(id) / source(file)                                                its scripts and their code
//   spawn(src id | prefab name, parent id) / destroy(id) / worldToParent(id, x, y, z) / toCamera(id)
//   raycast(x, y) / planes() / scene() / loadScene(i) / log(level, file, line, text)
// Keep in step with diorama.js: a script must do the same in both.
(function (G) {
  'use strict';
  var D = G.__dio;
  var R = { objects: [], go: {}, t: 0, dt: 0 };
  var input = { tapped: false, x: 0, y: 0 };
  var D2R = Math.PI / 180;

  function report(level, file, line, text) { D.log(level, file || '', line || 0, String(text)); }
  function lineOf(err) {   // the script's own line: QuickJS's stack names the wrapper's (3 lines before the script)
    var m = String(err && err.stack || '').match(/<input>:(\d+)/) || String(err && err.stack || '').match(/:(\d+):\d+/);
    return m ? Math.max(1, parseInt(m[1], 10) - 3) : 0;
  }
  function vec(get, set) {   // x, y, z bound to the object's own numbers
    var v = {};
    ['x', 'y', 'z'].forEach(function (k, i) { Object.defineProperty(v, k, { get: function () { return get(i); }, set: function (n) { set(i, +n); }, enumerable: true }); });
    v.set = function (x, y, z) { set(0, +x); set(1, +y); set(2, +z); return v; };
    v.toArray = function () { return [get(0), get(1), get(2)]; };
    return v;
  }
  function field(id, key) { return vec(function (i) { return D.getv(id, key, i); }, function (i, n) { D.setv(id, key, i, n); }); }
  function prop(obj, name, id, key, cast) {
    Object.defineProperty(obj, name, { get: function () { return D.get(id, key); }, set: function (v) { D.set(id, key, cast ? cast(v) : v); }, enumerable: true });
  }
  var num = function (v) { return +v; }, bool = function (v) { return !!v; }, str = function (v) { return String(v); };

  function GameObject(id) {
    var self = this;
    this.id = id; this.name = D.get(id, 'name');
    this.transform = { position: field(id, 'position'), rotation: field(id, 'rotation'), scale: field(id, 'scale') };
    this.position = this.transform.position; this.rotation = this.transform.rotation; this.scale = this.transform.scale;
    var vel = [0, 0, 0]; this._vel = vel;
    this.velocity = vec(function (i) { return vel[i]; }, function (i, n) { vel[i] = n; });
    Object.defineProperty(this, 'active', { get: function () { return D.get(id, 'active'); }, set: function (v) { D.set(id, 'active', !!v); } });
    this.scripts = [];
    this.uiListeners = {};
    void self;
  }
  // the media the native player doesn't play yet: there, so scripts that use them keep going
  function quietMedia() { return { play: function () {}, pause: function () {}, stop: function () {}, playing: false, volume: 1, loop: false, time: 0, duration: 0, speed: 1, muted: false, element: null }; }
  GameObject.prototype.getComponent = function (type) {
    var id = this.id, go = this, t = String(type).toLowerCase(), c;
    switch (t) {
    case 'transform': return this.transform;
    case 'animate':
      if (!D.get(id, 'hasAnim')) return null;
      c = {}; prop(c, 'on', id, 'animOn', bool); prop(c, 'does', id, 'anim', str); prop(c, 'speed', id, 'animSpeed', num); prop(c, 'amount', id, 'animAmount', num);
      return c;
    case 'material':
      c = { colour: field(id, 'colour') };
      prop(c, 'metallic', id, 'metallic', num); prop(c, 'roughness', id, 'roughness', num); prop(c, 'emissiveStrength', id, 'emissiveStrength', num);
      Object.defineProperty(c, 'opacity', { get: function () { return D.getv(id, 'colour', 3); }, set: function (v) { D.setv(id, 'colour', 3, +v); } });
      return c;
    case 'words': if (D.get(id, 'kind') !== 'Words') return null; c = {}; prop(c, 'text', id, 'text', str); return c;
    case 'model':
      if (D.get(id, 'kind') !== 'Model') return null;
      c = { play: function (name) { D.set(id, 'clip', String(name)); } }; Object.defineProperty(c, 'clip', { get: function () { return D.get(id, 'clip'); } });
      return c;
    case 'canvas': if (!D.get(id, 'hasCanvas')) return null; c = { element: null }; prop(c, 'visible', id, 'active', bool); return c;
    case 'text': case 'button': case 'slider': case 'toggle': case 'image': case 'panel':
      if (String(D.get(id, 'ui')).toLowerCase() !== t) return null;
      var on = function (fn) { return function (f) { (go.uiListeners[fn] = go.uiListeners[fn] || []).push(f); }; };
      c = { element: null, onClick: on('onClick'), onChange: on('onChange'), position: field(id, 'uiRect'), colour: field(id, 'uiColour') };
      prop(c, 'on', id, 'uiOn', bool); prop(c, 'text', id, 'uiText', str); prop(c, 'value', id, 'uiValue', num); prop(c, 'isOn', id, 'uiChecked', bool); prop(c, 'picture', id, 'uiFile', str);
      return c;
    case 'light':
      if (!D.get(id, 'hasLight')) return null;
      c = { colour: field(id, 'lightColour') }; prop(c, 'on', id, 'lightOn', bool); prop(c, 'intensity', id, 'lightIntensity', num); prop(c, 'range', id, 'lightRange', num);
      return c;
    case 'audio': case 'audiosource': return D.get(id, 'hasAudio') ? quietMedia() : null;
    case 'video': case 'videoplayer': return D.get(id, 'hasVideo') ? quietMedia() : null;
    case 'particles': case 'particlesystem':
      if (!D.get(id, 'hasParticles')) return null;
      c = { colourStart: field(id, 'pColourStart'), colourEnd: field(id, 'pColourEnd') }; prop(c, 'on', id, 'particlesOn', bool); prop(c, 'speed', id, 'pSpeed', num); prop(c, 'gravity', id, 'pGravity', num);
      return c;
    case 'line': case 'linerenderer':
      if (!D.get(id, 'hasLine')) return null;
      c = { colour: field(id, 'lineColour'),
            point: function (i) { return vec(function (k) { return D.getv(id, 'linePoints', i * 3 + k); }, function (k, n) { D.setv(id, 'linePoints', i * 3 + k, n); }); } };
      prop(c, 'on', id, 'lineOn', bool); prop(c, 'width', id, 'lineWidth', num);
      Object.defineProperty(c, 'count', { get: function () { return D.get(id, 'linePointCount'); } });
      return c;
    case 'trail': case 'trailrenderer':
      if (!D.get(id, 'hasTrail')) return null;
      c = { clear: function () {} }; prop(c, 'on', id, 'trailOn', bool); prop(c, 'time', id, 'trailTime', num); prop(c, 'width', id, 'trailWidth', num);
      return c;
    }
    for (var i = 0; i < this.scripts.length; ++i) if (this.scripts[i].name.toLowerCase() === t) return this.scripts[i].self;
    return null;
  };
  GameObject.prototype.toCamera = function () { return JSON.parse(D.toCamera(this.id)); };
  GameObject.prototype.lookAtCamera = function () {
    var d = this.toCamera();
    D.setv(this.id, 'rotation', 1, Math.atan2(d.x, d.z) / D2R);
    D.setv(this.id, 'rotation', 0, -Math.asin(Math.max(-1, Math.min(1, d.y))) / D2R);
  };
  GameObject.prototype.find = function (path) { return find(path, this.id); };
  GameObject.prototype.toString = function () { return '[' + this.name + ']'; };

  function wrap(id) { if (!R.go[id]) R.go[id] = new GameObject(id); return R.go[id]; }
  function find(path, from) {
    var parts = String(path).split('/'), hit = null;
    var search = function (n, i) {
      if (hit !== null) return;
      D.children(n).forEach(function (c) {
        if (hit !== null) return;
        if (D.get(c, 'name') === parts[i]) { if (i === parts.length - 1) hit = c; else search(c, i + 1); }
        else if (i === 0) search(c, 0);
      });
    };
    var rs = JSON.parse(D.roots()), roots;
    if (from !== undefined) roots = [from];
    else {
      roots = rs.map(function (r) { return r.id; });
      if (parts.length > 1) { var r0 = rs.filter(function (r) { return r.name === parts[0]; })[0]; if (r0) { roots = [r0.id]; parts = parts.slice(1); } }
    }
    roots.forEach(function (r) { search(r, 0); });
    return hit !== null ? wrap(hit) : null;
  }
  function sceneInfo() { return JSON.parse(D.scene()); }
  function loadScene(which) {
    var s = sceneInfo(), i = typeof which === 'number' ? which : s.names.indexOf(String(which));
    if (i < 0 || i >= s.count) { report('error', '', 0, 'loadScene: no scene ' + JSON.stringify(which)); return; }
    D.loadScene(i);
  }
  var cameraApi = { get position() { return { x: 0, y: 0, z: 0 }; } };   // the camera is where the world starts
  var navWarned = false;
  function navStub() { if (!navWarned) { navWarned = true; report('log', '', 0, 'nav: walked paths are in the web game; the native player draws and follows the paths made in the editor'); } }
  var nav = { recording: false, record: navStub, save: navStub, toggle: function () { navStub(); return 'idle'; }, follow: navStub, stopFollowing: navStub, reset: navStub,
              paths: function () { return []; }, points: function () { return []; }, forget: navStub };
  var planes = { get list() { return JSON.parse(D.planes()); }, raycast: function (x, y) { return JSON.parse(D.raycast(+x, +y)); } };
  function attachScripts(id) {
    var go = wrap(id);
    JSON.parse(D.scripts(id)).forEach(function (sc) {
      if (sc.on === false) return;
      var code = D.source(sc.file);
      if (code === null || code === undefined) { report('error', sc.file, 0, 'not found'); return; }
      var name = sc.file.split('/').pop().replace(/\.js$/, '');
      try {
        var f = new Function('gameObject', 'find', 'spawn', 'destroy', 'log', 'time', 'camera', 'input', 'loadScene', 'scene', 'nav', 'planes',
          '"use strict";\n' + code + '\n;return { start: typeof start === "function" ? start : null, update: typeof update === "function" ? update : null,' +
          ' onTap: typeof onTap === "function" ? onTap : null, onFound: typeof onFound === "function" ? onFound : null, onLost: typeof onLost === "function" ? onLost : null,' +
          ' onClick: typeof onClick === "function" ? onClick : null, onChange: typeof onChange === "function" ? onChange : null,' +
          ' onPlanesChanged: typeof onPlanesChanged === "function" ? onPlanesChanged : null, onArrived: typeof onArrived === "function" ? onArrived : null };');
        var log = function () { report('log', sc.file, 0, Array.prototype.map.call(arguments, function (a) { return typeof a === 'object' ? JSON.stringify(a) : String(a); }).join(' ')); };
        var api = f.call(go, go, function (p) { return find(p); }, spawn, destroy, log, function () { return R.t; }, cameraApi, input, loadScene, sceneInfo(), nav, planes);
        go.scripts.push({ name: name, file: sc.file, fns: api, self: go, started: false });
      } catch (e) { report('error', sc.file, lineOf(e), e.message); }
    });
    R.objects.push(go);
  }
  function call(go, s, fn, arg) {
    if (!s.fns[fn]) return;
    try { s.fns[fn].call(go, arg); } catch (e) { report('error', s.file, lineOf(e), e.message); s.fns[fn] = null; }
  }
  function spawn(what, at) {
    var src = what && what.id !== undefined ? what.id : null, made;
    if (src === null) { var hit = find(what); if (hit) src = hit.id; }
    var parent = at && at.id !== undefined ? D.parent(at.id) : undefined;
    made = JSON.parse(D.spawn(src !== null ? src : String(what), parent === undefined ? 'same' : parent));
    if (!made.length) { report('error', '', 0, 'spawn: nothing called ' + what); return null; }
    made.forEach(function (id) { attachScripts(id); });
    var first = wrap(made[0]);
    if (at && at.position) first.position.set(at.position.x, at.position.y, at.position.z);
    if (at && at.worldPosition) { var l = JSON.parse(D.worldToParent(made[0], +at.worldPosition.x, +at.worldPosition.y, +at.worldPosition.z)); first.position.set(l.x, l.y, l.z); }
    return first;
  }
  function destroy(go) {
    if (!go || go.id === undefined) return;
    JSON.parse(D.destroy(go.id)).forEach(function (id) { if (R.go[id]) { R.go[id].dead = true; delete R.go[id]; } });
  }

  // ---- what the player calls
  G.__start = function () {   // every object active at the start, parents first (as the web game builds them)
    JSON.parse(D.startObjects()).forEach(function (id) { attachScripts(id); });
  };
  G.__frame = function (dt, t, tapped, x, y) {
    R.t = t; R.dt = dt;
    input.tapped = !!tapped; input.x = x; input.y = y;
    R.objects.forEach(function (g) {
      if (g.dead) return;
      g.scripts.forEach(function (s) { if (!s.started) { s.started = true; call(g, s, 'start'); } call(g, s, 'update', dt); });
      var v = g._vel; if (v[0] || v[1] || v[2]) { D.setv(g.id, 'position', 0, D.getv(g.id, 'position', 0) + v[0] * dt); D.setv(g.id, 'position', 1, D.getv(g.id, 'position', 1) + v[1] * dt); D.setv(g.id, 'position', 2, D.getv(g.id, 'position', 2) + v[2] * dt); }
    });
    R.objects = R.objects.filter(function (g) { return !g.dead; });
    input.tapped = false;
  };
  G.__seen = function (rootId, on) {   // an anchor found or lost: its objects' onFound / onLost
    JSON.parse(D.descendants(rootId)).forEach(function (id) { var g = R.go[id]; if (g && !g.dead) g.scripts.forEach(function (s) { call(g, s, on ? 'onFound' : 'onLost'); }); });
  };
  G.__tap = function (id) {   // a tap on an object: the nearest one up its parents with an onTap
    for (var n = id; n !== undefined && n !== null && n >= 0; n = D.parent(n)) {
      var g = R.go[n];
      if (g && !g.dead && g.scripts.some(function (s) { return s.fns.onTap; })) { g.scripts.forEach(function (s) { call(g, s, 'onTap'); }); return; }
    }
  };
  G.__ui = function (id, what, value) {   // the Canvas: a button pressed, a slider or toggle changed
    var g = wrap(id);
    if (what === 'onClick') {
      var line = D.get(id, 'uiOnClick');
      if (line) try { new Function('nav', 'find', 'loadScene', 'log', 'button', line)(nav, function (p) { return find(p); }, loadScene, function () { report('log', '', 0, Array.prototype.join.call(arguments, ' ')); }, g); }
                catch (e) { report('error', g.name + ' On Click', 0, e.message); }
    }
    g.scripts.forEach(function (s) { call(g, s, what, value); });
    (g.uiListeners[what] || []).forEach(function (f) { try { f(value); } catch (e) { report('error', g.name, 0, e.message); } });
  };
  G.__planesChanged = function (json) {
    var ev = JSON.parse(json);
    R.objects.forEach(function (g) { if (!g.dead) g.scripts.forEach(function (sc) { call(g, sc, 'onPlanesChanged', ev); }); });
  };
  G.__arrived = function (id, path) { var g = R.go[id]; if (g && !g.dead) g.scripts.forEach(function (s) { call(g, s, 'onArrived', path); }); };
})(globalThis);
