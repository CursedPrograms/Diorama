// Diorama's player: builds a Diorama scene (scene.json / scene.js) in a phone's browser with three.js, on what the game
// tracks: printed markers (jsartoolkit5), pictures (MindAR image tracking) or the player's face (MindAR face tracking).
// The scene building is the same for all of them (and works with the three.js each one uses); only the starters
// differ. Anchor space as in the editor: Y up, +Z towards the viewer; a marker or picture is 1 across, a face is 1 wide.
(function () {
  'use strict';
  var D2R = Math.PI / 180;

  function loadJSON(url, cb) {
    var x = new XMLHttpRequest();
    x.open('GET', url); x.onload = function () { cb(JSON.parse(x.responseText)); }; x.send();
  }
  function hash(i, k) {   // the same pseudo-random numbers as the editor's flocks
    var h = (Math.imul(i, 0x8da6b343) ^ Math.imul(k, 0xd8163841)) >>> 0;
    h = (h ^ (h >>> 13)) >>> 0; h = Math.imul(h, 0x5bd1e995) >>> 0; h = (h ^ (h >>> 15)) >>> 0;
    return (h & 0xffffff) / 16777215;
  }
  function say(t) { var h = document.getElementById('hint'); if (h) h.firstChild ? (h.firstChild.textContent = t + ' ') : (h.textContent = t); }

  // ---- the scene, with whichever three.js the tracker brings
  function core(THREE, GLTFLoader) {
    var texLoader = new THREE.TextureLoader(), textures = {};
    var srgb = function (t) { if (THREE.SRGBColorSpace) t.colorSpace = THREE.SRGBColorSpace; return t; };
    function texture(file, onAspect) {
      if (!textures[file]) textures[file] = srgb(texLoader.load(file, function (t) { t.needsUpdate = true; if (onAspect) onAspect(t.image.height / t.image.width); }));
      else if (onAspect && textures[file].image) onAspect(textures[file].image.height / textures[file].image.width);
      return textures[file];
    }
    function canvasTexture(draw, w, h) {
      var c = document.createElement('canvas'); c.width = w; c.height = h;
      draw(c.getContext('2d'), w, h);
      return srgb(new THREE.CanvasTexture(c));
    }
    var rayTex = canvasTexture(function (g, w, h) {   // bright down the middle, soft at the sides, fading downwards
      var img = g.createImageData(w, h);
      for (var y = 0; y < h; ++y) for (var x = 0; x < w; ++x) {
        var u = Math.abs(x / (w / 2 - 0.5) - 1), v = y / (h - 1);
        var a = Math.pow(Math.max(0, 1 - u), 2) * (1 - v) * (0.3 + 0.7 * Math.min(1, v * 6));
        var i = (y * w + x) * 4; img.data[i] = img.data[i + 1] = img.data[i + 2] = 255; img.data[i + 3] = a * 255;
      }
      g.putImageData(img, 0, 0);
    }, 64, 64);
    function attr(g, name, arr, n) { var a = new THREE.BufferAttribute(new Float32Array(arr), n); (g.setAttribute || g.addAttribute).call(g, name, a); }

    function material(o, map) {
      var opts = { color: new THREE.Color(o.colour[0], o.colour[1], o.colour[2]), opacity: o.colour[3], side: THREE.DoubleSide,
                   transparent: true, alphaTest: 0.01 };
      if (map) opts.map = map;
      if (o.glow) { opts.blending = THREE.AdditiveBlending; opts.depthWrite = false; }
      return o.lit ? new THREE.MeshLambertMaterial(opts) : new THREE.MeshBasicMaterial(opts);
    }
    function capsule() {   // a lathe of two half circles and a tube, 2 high, 1 across
      var pts = [], i;
      for (i = 0; i <= 8; ++i) { var a = -Math.PI / 2 + i * Math.PI / 16; pts.push(new THREE.Vector2(0.5 * Math.cos(a), -0.5 + 0.5 * Math.sin(a))); }
      for (i = 0; i <= 8; ++i) { var b = i * Math.PI / 16; pts.push(new THREE.Vector2(0.5 * Math.cos(b), 0.5 + 0.5 * Math.sin(b))); }
      return new THREE.LatheGeometry(pts, 32);
    }

    // one object (its own node; its transform and animation are set each frame)
    function build(o, players) {
      var node = new THREE.Object3D(), geo, mesh;
      node.name = o.name;
      var file = o.file || '';
      var look = o.lookOn !== false;
      switch (look ? o.kind : '') {
      case 'Plane': geo = new THREE.PlaneGeometry(10, 10); geo.rotateX(-Math.PI / 2); break;
      case 'Cube': geo = new THREE.BoxGeometry(1, 1, 1); break;
      case 'Sphere': geo = new THREE.SphereGeometry(0.5, 32, 16); break;
      case 'Capsule': geo = capsule(); break;
      case 'Cylinder': geo = new THREE.CylinderGeometry(0.5, 0.5, 2, 32); break;
      case 'Quad': geo = new THREE.PlaneGeometry(1, 1); geo.rotateY(Math.PI); break;
      }
      if (geo) { mesh = new THREE.Mesh(geo, material(o, file ? texture(file) : null)); node.add(mesh); }
      // pictures and words that face the phone are sprites (three.js turns them to the camera)
      var facing = function (map, aspect) {
        var sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: map, transparent: true, opacity: o.colour[3],
          color: new THREE.Color(o.colour[0], o.colour[1], o.colour[2]), blending: o.glow ? THREE.AdditiveBlending : THREE.NormalBlending }));
        sp.scale.set(1, aspect, 1); node.add(sp); return sp;
      };
      if (look && o.kind === 'Picture' && file) {
        if (o.billboard) { var s1 = facing(texture(file, function (a) { s1.scale.y = a; }), 1); }
        else {
          var pg = new THREE.PlaneGeometry(1, 1); pg.rotateX(-Math.PI / 2);
          mesh = new THREE.Mesh(pg, material(o, texture(file, function (aspect) { mesh.scale.z = aspect; })));
          node.add(mesh);
        }
      }
      if (look && o.kind === 'Words') {
        var cw = 1024, ch = 160;
        var tt = canvasTexture(function (g, w, h) {
          g.font = 'bold 110px Segoe UI, Arial, sans-serif'; g.fillStyle = '#fff'; g.textAlign = 'center'; g.textBaseline = 'middle';
          g.fillText(o.text, w / 2, h / 2);
        }, cw, ch);
        if (o.billboard) facing(tt, ch / cw);
        else { mesh = new THREE.Mesh(new THREE.PlaneGeometry(1, ch / cw), material({ colour: o.colour, lit: false, glow: o.glow }, tt)); node.add(mesh); }
      }
      if (look && o.kind === 'Model' && file) {
        new GLTFLoader().load(file, function (g) {
          node.add(g.scene);
          if (o.occluder) { occlude(g.scene); return; }
          g.scene.traverse(function (c) {
            if (c.isMesh && c.material) {
              var ms = Array.isArray(c.material) ? c.material : [c.material];
              ms.forEach(function (m) {
                if (m.color) m.color.multiply(new THREE.Color(o.colour[0], o.colour[1], o.colour[2]));
                m.side = THREE.DoubleSide;
                // glTF counts a model with no material as metal; without reflections that draws black: plain instead
                if (m.metalness !== undefined && !m.metalnessMap && !m.envMap) m.metalness = 0;
              });
            }
          });
          if (o.clip && g.animations.length) {
            var clip = g.animations.filter(function (a) { return a.name === o.clip; })[0] || null;
            if (clip) {
              var mixer = new THREE.AnimationMixer(g.scene), act = mixer.clipAction(clip);
              act.setLoop(o.clipLoop ? THREE.LoopRepeat : THREE.LoopOnce); act.clampWhenFinished = true; act.timeScale = o.clipSpeed; act.play();
              players.push(mixer);
            }
          }
        });
      }
      if (look && o.kind === 'Sprites' && file) {
        var tex = texture(file);
        node.userData.sprites = [];
        for (var i = 0; i < o.count; ++i) {
          var sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, transparent: true, depthWrite: false }));
          node.userData.sprites.push(sp); node.add(sp);
        }
      }
      if (look && o.kind === 'Light rays') {
        node.userData.rays = [];
        for (var r = 0; r < o.rays; ++r) {
          var rg = new THREE.BufferGeometry();
          attr(rg, 'position', [-0.02, 0, 0, 0.02, 0, 0, 0.12, -1, 0, -0.12, -1, 0], 3);
          attr(rg, 'uv', [0, 1, 1, 1, 1, 0, 0, 0], 2);
          rg.setIndex([0, 1, 2, 0, 2, 3]);
          var rm = new THREE.Mesh(rg, new THREE.MeshBasicMaterial({ map: rayTex, color: new THREE.Color(o.colour[0], o.colour[1], o.colour[2]),
            transparent: true, opacity: o.colour[3], blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
          node.userData.rays.push(rm); node.add(rm);
        }
      }
      components(o, node);
      node.userData.o = o;
      return node;
    }

    // ---------- components: Occluder, Light, Audio Source, Video Player, Particle System, Line and Trail Renderer ----------
    var K = THREE.SRGBColorSpace ? Math.PI : 1;   // three's newer lights are in physical units
    var dotTex = canvasTexture(function (g, w, h) {   // a soft dot: particles with no picture
      var gr = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, w / 2);
      gr.addColorStop(0, 'rgba(255,255,255,1)'); gr.addColorStop(0.4, 'rgba(255,255,255,0.6)'); gr.addColorStop(1, 'rgba(255,255,255,0)');
      g.fillStyle = gr; g.fillRect(0, 0, w, h);
    }, 64, 64);
    var media = [];   // sounds and videos: phones start them only after a tap
    var listener = null;
    function getListener() { if (!listener) { listener = new THREE.AudioListener(); if (R.camera) R.camera.add(listener); } return listener; }
    function occlude(obj) {   // invisible, but hides what's behind it
      obj.traverse(function (c) { if (c.isMesh || c.isSprite) { c.material = new THREE.MeshBasicMaterial({ colorWrite: false, side: THREE.DoubleSide }); c.renderOrder = -1; } });
    }
    function segmentMesh() { return new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshBasicMaterial({ transparent: true, depthWrite: false })); }
    var UP = new THREE.Vector3(0, 1, 0);
    function placeSegment(mesh, a, b, width) {   // a thin box from a to b
      var d = new THREE.Vector3().subVectors(b, a), len = d.length();
      mesh.visible = len > 1e-5; if (!mesh.visible) return;
      mesh.position.addVectors(a, b).multiplyScalar(0.5);
      mesh.quaternion.setFromUnitVectors(UP, d.divideScalar(len));
      mesh.scale.set(width, len, width);
    }
    function components(o, node) {
      if (o.occluder) occlude(node);
      if (o.hasLight) {
        var col = new THREE.Color(o.lightColour[0], o.lightColour[1], o.lightColour[2]), li;
        if (o.lightType === 1) li = new THREE.SpotLight(col, o.lightIntensity * K, o.lightRange, o.spotAngle * D2R / 2, 0.15, 2);
        else if (o.lightType === 2) li = new THREE.DirectionalLight(col, o.lightIntensity * K);
        else li = new THREE.PointLight(col, o.lightIntensity * K, o.lightRange, 2);
        if (li.target) { var tg = new THREE.Object3D(); tg.position.set(0, 0, -1); node.add(tg); li.target = tg; }
        li.visible = o.lightOn !== false;
        node.add(li); node.userData.light = li;
      }
      if (o.hasAudio && o.audioFile) {
        var snd = o.spatial ? new THREE.PositionalAudio(getListener()) : new THREE.Audio(getListener());
        if (o.spatial) { snd.setRefDistance(o.audioNear); snd.setMaxDistance(o.audioFar); snd.setDistanceModel('linear'); }
        snd.setLoop(!!o.audioLoop); snd.setVolume(o.volume);
        new THREE.AudioLoader().load(o.audioFile, function (buf) { snd.setBuffer(buf); if (snd.userData.want) tryPlay(snd); });
        snd.userData = { kind: 'audio', auto: o.playOnStart && o.audioOn !== false };
        node.add(snd); node.userData.audio = snd; media.push(snd);
      }
      if (o.hasVideo && o.videoFile) {
        var v = document.createElement('video');
        v.src = o.videoFile; v.loop = !!o.videoLoop; v.muted = !!o.videoMuted; v.volume = o.videoVolume; v.playbackRate = o.videoSpeed || 1;
        v.playsInline = true; v.setAttribute('playsinline', ''); v.setAttribute('webkit-playsinline', ''); v.crossOrigin = 'anonymous'; v.preload = 'auto';
        var vt = srgb(new THREE.VideoTexture(v));
        node.traverse(function (c) { if ((c.isMesh || c.isSprite) && c.material && !o.occluder) { c.material.map = vt; c.material.needsUpdate = true; } });
        v.userData = { kind: 'video', auto: o.playOnStart !== false && o.videoPlayOnStart !== false && o.videoOn !== false };
        node.userData.video = v; media.push(v);
      }
      if (o.hasParticles) {
        var n = Math.min(600, Math.max(1, Math.ceil(o.pRate * o.pLife)));
        var map = o.particleFile ? texture(o.particleFile) : dotTex, ps = [];
        for (var i = 0; i < n; ++i) {
          var sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: map, transparent: true, depthWrite: false,
            blending: o.pGlow ? THREE.AdditiveBlending : THREE.NormalBlending }));
          sp.visible = false; ps.push(sp); node.add(sp);
        }
        node.userData.particles = ps;
      }
      if (o.hasLine && o.linePoints && o.linePoints.length >= 6) {
        var segs = [], np = o.linePoints.length / 3;
        for (var k = 0; k + 1 < np + (o.lineLoop ? 1 : 0); ++k) { var m = segmentMesh(); segs.push(m); node.add(m); }
        node.userData.line = segs;
      }
      if (o.hasTrail) node.userData.trail = { segs: [], past: [] };
    }
    function tryPlay(m) {
      m.userData.want = true;
      if (m.userData.kind === 'video') { var pr = m.play(); if (pr && pr.catch) pr.catch(function () {}); return; }
      if (!m.buffer || m.isPlaying) return;
      if (m.context.state !== 'running') { m.context.resume(); }
      try { m.play(); } catch (e) {}
    }
    function stopMedia(m, rewind) {
      m.userData.want = false;
      if (m.userData.kind === 'video') { m.pause(); if (rewind) m.currentTime = 0; }
      else if (m.isPlaying) { if (rewind) m.stop(); else m.pause(); }
    }
    function startMediaUnder(root) {   // an anchor found: what plays on start starts (once)
      root.traverse(function (c) {
        [c.userData.audio, c.userData.video].forEach(function (m) { if (m && m.userData.auto && !m.userData.started) { m.userData.started = true; tryPlay(m); } });
      });
    }
    function unlockMedia() {   // the first tap: what wanted to play (and was refused) plays
      if (listener && listener.context.state !== 'running') listener.context.resume();
      media.forEach(function (m) {
        if (!m.userData.want) return;
        if (m.userData.kind === 'video') { if (m.paused) tryPlay(m); } else if (!m.isPlaying) tryPlay(m);
      });
    }
    function anchorOf(node) { var n = node; while (n.parent && n.parent.userData && n.parent.userData.o) n = n.parent; return n.parent || n; }
    var tmpA = new THREE.Vector3(), tmpB = new THREE.Vector3();
    function animateComponents(node, t) {
      var o = node.userData.o;
      if (node.userData.light) node.userData.light.visible = o.lightOn !== false;
      var ps = node.userData.particles;
      if (ps) {   // each slot k is born at k / rate and lives pLife, then again: the editor's maths
        var n = ps.length, T = n / Math.max(0.01, o.pRate), spread = o.pSpread * D2R;
        var sx = Math.max(1e-6, o.scale[0]), sy = Math.max(1e-6, o.scale[1]);
        for (var k = 0; k < n; ++k) {
          var sp = ps[k], since = t - k / o.pRate;
          if (o.particlesOn === false || since < 0) { sp.visible = false; continue; }
          var cycle = Math.floor(since / T), age = since - cycle * T;
          if (age > o.pLife) { sp.visible = false; continue; }
          var u = age / Math.max(0.001, o.pLife), seed = k * 131 + cycle * 7919;
          var az = hash(seed, 1) * 6.2831853, el = spread * Math.sqrt(hash(seed, 2));
          var spd = o.pSpeed * (0.7 + 0.6 * hash(seed, 3));
          sp.position.set(Math.sin(el) * Math.cos(az) * spd * age, Math.cos(el) * spd * age - 0.5 * o.pGravity * age * age, Math.sin(el) * Math.sin(az) * spd * age);
          var a = o.pColourStart, b = o.pColourEnd;
          sp.material.color.setRGB(a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u);
          sp.material.opacity = a[3] + (b[3] - a[3]) * u;
          var sz = o.pSizeStart + (o.pSizeEnd - o.pSizeStart) * u;
          sp.scale.set(sz / sx, sz / sy, 1); sp.visible = true;
        }
      }
      var segs = node.userData.line;
      if (segs) {
        var lp = o.linePoints, np = lp.length / 3, avg = Math.max(1e-6, (o.scale[0] + o.scale[1] + o.scale[2]) / 3);
        segs.forEach(function (m, i) {
          var j = (i + 1) % np;
          m.visible = o.lineOn !== false;
          m.material.color.setRGB(o.lineColour[0], o.lineColour[1], o.lineColour[2]); m.material.opacity = o.lineColour[3];
          if (m.visible) placeSegment(m, tmpA.set(lp[i * 3], lp[i * 3 + 1], lp[i * 3 + 2]), tmpB.set(lp[j * 3], lp[j * 3 + 1], lp[j * 3 + 2]), o.lineWidth / avg);
        });
      }
      var tr = node.userData.trail;
      if (tr) {   // where it really was (scripts move things too), in its anchor's space
        var root = anchorOf(node);
        node.updateMatrixWorld(true);
        var here = root.worldToLocal(node.getWorldPosition(new THREE.Vector3()));
        tr.past.unshift({ t: t, p: here });
        while (tr.past.length > 2 && t - tr.past[tr.past.length - 1].t > o.trailTime) tr.past.pop();
        var steps = 20;
        while (tr.segs.length < steps) { var m = segmentMesh(); root.add(m); tr.segs.push(m); }
        var at = function (tt) {   // its place at time tt, between the samples
          for (var q = 0; q + 1 < tr.past.length; ++q) if (tr.past[q + 1].t <= tt) {
            var A = tr.past[q], B = tr.past[q + 1], f = (A.t - tt) / Math.max(1e-6, A.t - B.t);
            return new THREE.Vector3().lerpVectors(A.p, B.p, f);
          }
          return tr.past[tr.past.length - 1].p.clone();
        };
        var prev = here;
        for (var i = 1; i <= steps; ++i) {
          var cur = at(t - o.trailTime * i / steps), m2 = tr.segs[i - 1];
          m2.visible = o.trailOn !== false && node.visible;
          m2.material.color.setRGB(o.trailColour[0], o.trailColour[1], o.trailColour[2]); m2.material.opacity = o.trailColour[3] * (1 - i / steps);
          if (m2.visible) placeSegment(m2, prev, cur, o.trailWidth * (1 - 0.8 * i / steps));
          prev = cur;
        }
      }
    }

    // the transform and what it does by itself (the same as the editor's)
    function animate(node, t) {
      var o = node.userData.o;
      var ph = t * o.animSpeed * Math.PI * 2;
      node.position.set(o.position[0], o.position[1], o.position[2]);
      node.rotation.set(o.rotation[0] * D2R, o.rotation[1] * D2R, o.rotation[2] * D2R, 'YXZ');
      node.scale.set(o.scale[0], o.scale[1], o.scale[2]);
      var fade = 1;
      var anim = (o.hasAnim === false || o.animOn === false) ? 'Still' : o.anim;
      if (anim === 'Float') node.position.y += Math.sin(ph) * o.animAmount;
      else if (anim === 'Spin') node.rotation.y += t * o.animSpeed * Math.PI * 2;
      else if (anim === 'Sway') node.rotation.z += Math.sin(ph) * o.animAmount * Math.PI / 2;
      else if (anim === 'Pulse') fade = 1 - o.animAmount * (0.5 + 0.5 * Math.sin(ph));
      node.children.forEach(function (c) {
        if (!c.material || node.userData.rays || c.isLight || (node.userData.particles && c.isSprite && node.userData.particles.indexOf(c) >= 0)) return;
        if (node.userData.line && node.userData.line.indexOf(c) >= 0) return;
        if (c.material.userData.base === undefined) c.material.userData.base = c.material.opacity;
        c.material.opacity = c.material.userData.base * fade;
      });
      if (node.userData.sprites) node.userData.sprites.forEach(function (sp, i) {
        var s = o.animSpeed * (0.6 + 0.8 * hash(i, 7)), p = hash(i, 1) * 6.28;
        sp.position.set(0.45 * Math.sin(t * s * 0.9 + p) * (0.6 + 0.4 * hash(i, 2)),
                        0.45 * Math.sin(t * s * 1.3 + p * 1.7) * (0.6 + 0.4 * hash(i, 3)),
                        0.45 * Math.cos(t * s * 0.7 + p * 0.6) * (0.6 + 0.4 * hash(i, 4)));
        var flap = o.flap > 0 ? 0.25 + 0.75 * Math.abs(Math.cos(t * 9 * o.flap + p * 3)) : 1;
        var sz = o.spriteSize * (0.7 + 0.6 * hash(i, 5));
        sp.scale.set(sz * flap / Math.max(1e-6, o.scale[0]), sz / Math.max(1e-6, o.scale[1]), 1);   // sized in the world
      });
      animateComponents(node, t);
      if (node.userData.rays) node.userData.rays.forEach(function (rm, i) {
        var n = node.userData.rays.length;
        rm.rotation.z = (i - (n - 1) / 2) * 0.22 + 0.05 * Math.sin(t * 0.7 + i);
        rm.position.z = (hash(i, 9) - 0.5) * 0.3;
        rm.material.opacity = o.colour[3] * (0.6 + 0.4 * Math.sin(t * 1.3 + i * 2.1));
      });
    }

    // the window into the paper: an invisible sheet with a hole, drawn first, so under the paper only the hole shows
    function portal(m) {
      var big = 20, w = m.portalSize[0] / 2, h = m.portalSize[1] / 2;
      var shape = new THREE.Shape();
      shape.moveTo(-big, -big); shape.lineTo(big, -big); shape.lineTo(big, big); shape.lineTo(-big, big); shape.lineTo(-big, -big);
      var hole = new THREE.Path();
      hole.moveTo(-w, -h); hole.lineTo(-w, h); hole.lineTo(w, h); hole.lineTo(w, -h); hole.lineTo(-w, -h);
      shape.holes.push(hole);
      var g = new THREE.ShapeGeometry(shape); g.rotateX(-Math.PI / 2);
      var mask = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ colorWrite: false, side: THREE.DoubleSide }));
      mask.renderOrder = -1;
      var box = new THREE.Mesh(new THREE.BoxGeometry(m.portalSize[0], 1.5, m.portalSize[1]),
        new THREE.MeshLambertMaterial({ color: 0x1f1f21, side: THREE.BackSide }));
      box.position.y = -0.75;
      var group = new THREE.Group(); group.add(mask); group.add(box);
      return group;
    }


    // ---------- scripts: JavaScript behaviours on objects (start, update, onTap, onFound, onLost) ----------
    var R = { nodes: null, players: null, roots: {}, objects: [], camera: null, dom: null, t: 0, tapped: null, nextId: 1e6 };
    function report(level, file, line, text) {
      var where = file ? file.split('/').pop() + (line ? ':' + line : '') + ': ' : '';
      (level === 'error' ? console.error : console.log)(where + text);
      if (level === 'error') say('Problem: ' + where + text);
      if (window.DIORAMA_PLAY) {   // Play from the editor: its Console shows it too
        try { fetch('/__log', { method: 'POST', body: JSON.stringify({ level: level, file: file || '', line: line || 0, text: String(text) }) }); } catch (e) {}
      }
    }
    function lineOf(err, file) {   // the script's own line, from the error's stack (the wrapper adds 3 lines)
      var m = String(err && err.stack || '').match(new RegExp(file.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ':(\\d+)'));
      return m ? Math.max(1, parseInt(m[1], 10) - 3) : 0;
    }
    function vec(arr, after) {   // x, y, z bound to the object's own numbers
      var v = {};
      ['x', 'y', 'z'].forEach(function (k, i) { Object.defineProperty(v, k, { get: function () { return arr[i]; }, set: function (n) { arr[i] = +n; if (after) after(); }, enumerable: true }); });
      v.set = function (x, y, z) { arr[0] = +x; arr[1] = +y; arr[2] = +z; if (after) after(); return v; };
      v.toArray = function () { return arr.slice(); };
      return v;
    }
    function worldPos(node) { var p = new THREE.Vector3(); node.getWorldPosition(p); return p; }

    // the object a script sees as this / gameObject
    function GameObject(node) {
      var o = node.userData.o, self = this;
      this.node = node; this.id = o.id; this.name = o.name; this.data = o;
      this.transform = { position: vec(o.position), rotation: vec(o.rotation), scale: vec(o.scale) };
      this.position = this.transform.position; this.rotation = this.transform.rotation; this.scale = this.transform.scale;
      this.velocity = vec([0, 0, 0]);
      Object.defineProperty(this, 'active', { get: function () { return node.visible; }, set: function (v) { node.visible = !!v; } });
      this.scripts = [];
    }
    GameObject.prototype.getComponent = function (type) {
      var o = this.data, node = this.node;
      switch (String(type).toLowerCase()) {
      case 'transform': return this.transform;
      case 'animate': return o.hasAnim ? { get on() { return o.animOn; }, set on(v) { o.animOn = !!v; }, get does() { return o.anim; }, set does(v) { o.anim = v; },
                                            get speed() { return o.animSpeed; }, set speed(v) { o.animSpeed = +v; }, get amount() { return o.animAmount; }, set amount(v) { o.animAmount = +v; } } : null;
      case 'material': return { colour: vec(o.colour), get opacity() { return o.colour[3]; }, set opacity(v) { o.colour[3] = +v; node.traverse(function (c) { if (c.material) { c.material.userData.base = +v; } }); } };
      case 'words': return o.kind === 'Words' ? { get text() { return o.text; }, set text(v) { o.text = String(v); rebuild(this.go); }, go: this } : null;
      case 'model': return o.kind === 'Model' ? { get clip() { return o.clip; }, play: function (name) { o.clip = name; rebuild(this.go); }, go: this } : null;
      case 'light': var li = node.userData.light; return li ? { get on() { return o.lightOn !== false; }, set on(v) { o.lightOn = !!v; },
          colour: vec(o.lightColour, function () { li.color.setRGB(o.lightColour[0], o.lightColour[1], o.lightColour[2]); }),
          get intensity() { return o.lightIntensity; }, set intensity(v) { o.lightIntensity = +v; li.intensity = +v * K; },
          get range() { return o.lightRange; }, set range(v) { o.lightRange = +v; li.distance = +v; } } : null;
      case 'audio': case 'audiosource': var au = node.userData.audio; return au ? { play: function () { tryPlay(au); }, pause: function () { stopMedia(au, false); },
          stop: function () { stopMedia(au, true); }, get playing() { return !!au.isPlaying; },
          get volume() { return o.volume; }, set volume(v) { o.volume = +v; au.setVolume(+v); },
          get loop() { return !!o.audioLoop; }, set loop(v) { o.audioLoop = !!v; au.setLoop(!!v); } } : null;
      case 'video': case 'videoplayer': var vi = node.userData.video; return vi ? { play: function () { tryPlay(vi); }, pause: function () { stopMedia(vi, false); },
          stop: function () { stopMedia(vi, true); }, get playing() { return !vi.paused; }, get time() { return vi.currentTime; }, set time(v) { vi.currentTime = +v; },
          get duration() { return vi.duration || 0; }, get speed() { return vi.playbackRate; }, set speed(v) { vi.playbackRate = +v; },
          get volume() { return vi.volume; }, set volume(v) { vi.volume = +v; }, get muted() { return vi.muted; }, set muted(v) { vi.muted = !!v; }, element: vi } : null;
      case 'particles': case 'particlesystem': return o.hasParticles ? { get on() { return o.particlesOn !== false; }, set on(v) { o.particlesOn = !!v; },
          get speed() { return o.pSpeed; }, set speed(v) { o.pSpeed = +v; }, get gravity() { return o.pGravity; }, set gravity(v) { o.pGravity = +v; },
          colourStart: vec(o.pColourStart), colourEnd: vec(o.pColourEnd) } : null;
      case 'line': case 'linerenderer': return o.hasLine ? { get on() { return o.lineOn !== false; }, set on(v) { o.lineOn = !!v; },
          get width() { return o.lineWidth; }, set width(v) { o.lineWidth = +v; }, colour: vec(o.lineColour),
          point: function (i) { var lp = o.linePoints, view = {};   // point i's x, y, z, bound to the line
            [0, 1, 2].forEach(function (k) { Object.defineProperty(view, k, { get: function () { return lp[i * 3 + k]; }, set: function (v) { lp[i * 3 + k] = v; } }); });
            return vec(view); },
          get count() { return o.linePoints.length / 3; } } : null;
      case 'trail': case 'trailrenderer': return o.hasTrail ? { get on() { return o.trailOn !== false; }, set on(v) { o.trailOn = !!v; },
          get time() { return o.trailTime; }, set time(v) { o.trailTime = +v; }, get width() { return o.trailWidth; }, set width(v) { o.trailWidth = +v; },
          clear: function () { node.userData.trail.past.length = 0; } } : null;
      }
      for (var i = 0; i < this.scripts.length; ++i) if (this.scripts[i].name.toLowerCase() === String(type).toLowerCase()) return this.scripts[i].self;
      return null;
    };
    // where the phone is, from this object: a direction (x, y, z, in its parent's space) and how far
    GameObject.prototype.toCamera = function () {
      if (!R.camera) return { x: 0, y: 0, z: 1, distance: 1 };
      var cam = new THREE.Vector3(); R.camera.getWorldPosition(cam);
      var me = worldPos(this.node), d = cam.sub(me);
      var parent = this.node.parent, q = new THREE.Quaternion();
      if (parent) { parent.getWorldQuaternion(q); d.applyQuaternion(q.invert ? q.invert() : q.inverse()); var s = new THREE.Vector3(); parent.getWorldScale(s); d.divide(s); }
      var len = d.length() || 1;
      return { x: d.x / len, y: d.y / len, z: d.z / len, distance: len };
    };
    GameObject.prototype.lookAtCamera = function () {
      var d = this.toCamera();
      this.data.rotation[1] = Math.atan2(d.x, d.z) / D2R;
      this.data.rotation[0] = -Math.asin(Math.max(-1, Math.min(1, d.y))) / D2R;
    };
    GameObject.prototype.find = function (path) { return find(path, this.node); };
    GameObject.prototype.toString = function () { return '[' + this.name + ']'; };

    function wrap(node) { if (!node.userData.go) node.userData.go = new GameObject(node); return node.userData.go; }
    function rebuild(go) {   // a look that changed (words, a clip): built again in place
      var node = go.node, o = go.data, fresh = build(o, R.players);
      while (node.children.length) { var c = node.children[0]; if (c.userData && c.userData.o) break; node.remove(c); }
      fresh.children.slice().forEach(function (c) { node.add(c); });
      ['sprites', 'rays', 'light', 'audio', 'video', 'particles', 'line', 'trail'].forEach(function (k) { node.userData[k] = fresh.userData[k]; });
    }
    function find(path, from) {
      var parts = String(path).split('/'), hit = null;
      var search = function (n, i) {
        if (hit) return;
        n.children.forEach(function (c) {
          if (hit || !c.userData.o) return;
          if (c.userData.o.name === parts[i]) { if (i === parts.length - 1) hit = c; else search(c, i + 1); }
          else if (i === 0) search(c, 0);
        });
      };
      var roots = from ? [from] : Object.keys(R.roots).map(function (k) { return R.roots[k]; });
      roots.forEach(function (r) { search(r, 0); });
      return hit ? wrap(hit) : null;
    }
    function attachScripts(node) {
      var o = node.userData.o, go = wrap(node);
      (o.scripts || []).forEach(function (sc) {
        if (sc.on === false) return;
        var code = (window.DIORAMA_SCRIPTS || {})[sc.file];
        if (code === undefined) { report('error', sc.file, 0, 'not found'); return; }
        var name = sc.file.split('/').pop().replace(/\.js$/, '');
        try {
          var f = new Function('gameObject', 'find', 'spawn', 'destroy', 'log', 'time', 'camera', 'input',
            '"use strict";\n' + code + '\n;return { start: typeof start === "function" ? start : null, update: typeof update === "function" ? update : null,' +
            ' onTap: typeof onTap === "function" ? onTap : null, onFound: typeof onFound === "function" ? onFound : null, onLost: typeof onLost === "function" ? onLost : null };\n//# sourceURL=' + sc.file);
          var log = function () { report('log', sc.file, 0, Array.prototype.map.call(arguments, function (a) { return typeof a === 'object' ? JSON.stringify(a) : String(a); }).join(' ')); };
          var api = f.call(go, go, function (p) { return find(p); }, spawn, destroy, log, function () { return R.t; }, R.cameraApi, R.input);
          go.scripts.push({ name: name, file: sc.file, fns: api, self: go, started: false });
        } catch (e) { report('error', sc.file, lineOf(e, sc.file), e.message); }
      });
      R.objects.push(go);
    }
    function call(go, s, fn, arg) {
      if (!s.fns[fn]) return;
      try { s.fns[fn].call(go, arg); } catch (e) { report('error', s.file, lineOf(e, s.file), e.message); s.fns[fn] = null; }
    }
    // a copy of an object (by name in the scene, or a prefab) and everything under it, made now
    function spawn(what, at) {
      // a prefab by that name first (like Instantiate(prefab)), else an object in the scene (spawned ones too)
      var srcNode = null, data = null;
      if (what && what.node) srcNode = what.node;
      else if (window.DIORAMA_PREFABS && window.DIORAMA_PREFABS[what]) data = window.DIORAMA_PREFABS[what];
      else { var hit = find(what); if (hit) srcNode = hit.node; }
      if (!srcNode && !data) { report('error', '', 0, 'spawn: nothing called ' + what); return null; }
      var list = data ? JSON.parse(JSON.stringify(data)) : [];
      if (srcNode) srcNode.traverse(function (c) { if (c.userData.o) list.push(JSON.parse(JSON.stringify(c.userData.o))); });   // it and its children, parents first
      var src = srcNode ? srcNode.userData.o : null;
      var remap = {}; list.forEach(function (o) { remap[o.id] = ++R.nextId; });
      var parentNode = at && at.node ? at.node.parent : (srcNode ? srcNode.parent : R.roots[Object.keys(R.roots)[0]]);
      var first = null, made = {};
      list.forEach(function (o, i) {
        var oldParent = o.parent;
        o.id = remap[o.id]; o.parent = i === 0 ? -1 : (remap[oldParent] || -1);
        var n = build(o, R.players);
        (i === 0 ? parentNode : made[o.parent]).add(n); made[o.id] = n;
        R.nodes.push(n); attachScripts(n);
        if (i === 0) first = wrap(n);
      });
      if (first && at && at.position) first.position.set(at.position.x, at.position.y, at.position.z);
      return first;
    }
    function destroy(go) {
      if (!go || !go.node) return;
      go.node.traverse(function (c) { if (c.userData.go) c.userData.go.dead = true; });
      if (go.node.parent) go.node.parent.remove(go.node);
      R.nodes = R.nodes.filter(function (n) { return !n.userData.go || !n.userData.go.dead; });
    }
    R.input = { tapped: false, x: 0, y: 0 };
    R.cameraApi = { get position() { var p = new THREE.Vector3(); if (R.camera) R.camera.getWorldPosition(p); return { x: p.x, y: p.y, z: p.z }; } };
    var runtime = {
      begin: function (nodes, players) { R.nodes = nodes; R.players = players; },
      setCamera: function (camera, dom) {
        R.camera = camera; R.dom = dom;
        if (listener && !listener.parent) camera.add(listener);
        var ray = new THREE.Raycaster();
        var tap = function (ev) {
          var r = dom.getBoundingClientRect(), pt = ev.touches ? ev.touches[0] : ev;
          var x = ((pt.clientX - r.left) / r.width) * 2 - 1, y = -((pt.clientY - r.top) / r.height) * 2 + 1;
          R.input.tapped = true; R.input.x = x; R.input.y = y;
          unlockMedia();
          ray.setFromCamera({ x: x, y: y }, camera);
          var targets = []; Object.keys(R.roots).forEach(function (k) { if (R.roots[k].visible !== false) targets.push(R.roots[k]); });
          var hits = ray.intersectObjects(targets, true);
          for (var i = 0; i < hits.length; ++i) {
            var n = hits[i].object; while (n && !(n.userData && n.userData.go)) n = n.parent;
            if (n && n.userData.go && n.userData.go.scripts.some(function (s) { return s.fns.onTap; })) { var g = n.userData.go; g.scripts.forEach(function (s) { call(g, s, 'onTap'); }); break; }
          }
        };
        dom.addEventListener('mousedown', tap); dom.addEventListener('touchstart', tap, { passive: true });
      },
      anchor: function (name, root, objects) { R.roots[name] = root; root.userData.objects = objects; },
      seen: function (name, on) {   // an anchor found or lost: its objects' onFound / onLost
        var root = R.roots[name]; if (!root) return;
        if (on) startMediaUnder(root);
        root.traverse(function (c) { var g = c.userData.go; if (g && !g.dead) g.scripts.forEach(function (s) { call(g, s, on ? 'onFound' : 'onLost'); }); });
      },
      update: function (dt, t) {
        R.t = t;
        R.objects.forEach(function (g) {
          if (g.dead) return;
          g.scripts.forEach(function (s) { if (!s.started) { s.started = true; call(g, s, 'start'); } call(g, s, 'update', dt); });
          var v = g.velocity; if (v.x || v.y || v.z) { var p = g.data.position; p[0] += v.x * dt; p[1] += v.y * dt; p[2] += v.z * dt; }
        });
        R.objects = R.objects.filter(function (g) { return !g.dead; });
        R.input.tapped = false;
      }
    };

    // an anchor's world: its lights, its window, its objects (nodes and animation players collected for the frame loop)
    function anchorWorld(m, nodes, players) {
      var root = new THREE.Group();
      var amb = new THREE.AmbientLight(new THREE.Color(m.ambient[0], m.ambient[1], m.ambient[2]), THREE.SRGBColorSpace ? Math.PI : 1);
      var sun = new THREE.DirectionalLight(new THREE.Color(m.sun[0], m.sun[1], m.sun[2]), m.sunStrength * (THREE.SRGBColorSpace ? Math.PI : 1));
      var sy = m.sunYaw * D2R, sp = m.sunPitch * D2R;
      sun.position.set(Math.cos(sp) * Math.sin(sy), Math.sin(sp), Math.cos(sp) * Math.cos(sy));
      root.add(amb); root.add(sun);
      if (m.portal) root.add(portal(m));
      var byId = {};
      m.objects.forEach(function (o) {
        if (!o.active) return;
        var parent = o.parent === -1 ? root : byId[o.parent];
        if (!parent) return;
        var n = build(o, players);
        byId[o.id] = n; parent.add(n); nodes.push(n);
        attachScripts(n);
      });
      runtime.begin(nodes, players);
      runtime.anchor(m.name, root, m.objects);
      return root;
    }
    return { anchorWorld: anchorWorld, animate: animate, runtime: runtime };
  }

  function fromDisk() {
    if (location.protocol !== 'file:') return false;
    // a page opened straight from the disk can't load its pictures and models: say how to play it
    say('Double-click Play.bat in this folder to play it here, or put the folder on a web host (https) for phones.');
    return true;
  }
  function withScene(cb) { if (window.DIORAMA_SCENE) cb(window.DIORAMA_SCENE); else loadJSON('scene.json', cb); }

  // ---- Markers: jsartoolkit5 (three r103, loaded as plain scripts)
  function startMarkers() {
    if (fromDisk()) return;
    say('Loading the scene...');
    withScene(function (proj) {
      say('Starting the camera...');
      document.title = proj.title;
      var C = core(THREE, THREE.GLTFLoader);
      window.ARThreeOnLoad = function () {
        ARController.getUserMediaThreeScene({
          maxARVideoSize: 640, cameraParam: 'data/camera_para.dat',
          onError: function (e) { say('The camera didn\'t start: ' + (e && (e.message || e.name) || e)); },
          onSuccess: function (arScene, arController) {
            say('Point the camera at the marker.');
            document.body.className = arController.orientation;
            arController.setPatternDetectionMode(artoolkit.AR_TEMPLATE_MATCHING_COLOR_AND_MATRIX);
            arController.setMatrixCodeType(artoolkit.AR_MATRIX_CODE_3x3);
            // the black-and-white split worked out from each picture (Otsu), not a fixed level: copes with dim or
            // uneven light on the paper
            try { arController.setThresholdMode(2); } catch (e) {}
            var renderer = new THREE.WebGLRenderer({ antialias: true });
            var fit = function () {
              var vw = arController.videoWidth, vh = arController.videoHeight;
              if (arController.orientation === 'portrait') { var w = window.innerWidth / vh * vw, h = window.innerWidth; renderer.setSize(w, h); }
              else renderer.setSize(window.innerWidth, window.innerWidth / vw * vh);
            };
            fit(); window.addEventListener('resize', fit);
            document.body.insertBefore(renderer.domElement, document.body.firstChild);
            C.runtime.setCamera(arScene.camera, renderer.domElement);
            var nodes = [], players = [], roots = [];
            proj.markers.forEach(function (m) {
              var root = C.anchorWorld(m, nodes, players);
              root.rotation.x = Math.PI / 2;   // the editor's Y-up, laid on ARToolKit's marker (Z up out of it)
              var add = function (markerRoot) { markerRoot.add(root); arScene.scene.add(markerRoot); markerRoot.userData.name = m.name; roots.push(markerRoot); };
              if (m.type === 'barcode') add(arController.createThreeBarcodeMarker(m.barcode, 1));
              else arController.loadMarker(m.patt, function (id) { add(arController.createThreeMarker(id, 1)); });
            });
            var clock = new THREE.Clock(), t = 0, lastLine = '';
            var tick = function () {
              var dt = clock.getDelta(); t += dt;
              arScene.process();
              // tracking flickers (a blurred frame, a glint): keep showing a marker for a moment where it last was
              roots.forEach(function (r) {
                if (r.visible) { r.userData.seen = t; r.userData.last = r.matrix.clone(); }
                else if (r.userData.last && t - r.userData.seen < 0.35) { r.visible = true; r.matrix.copy(r.userData.last); }
              });
              roots.forEach(function (r) { if (!!r.userData.was !== r.visible) { r.userData.was = r.visible; C.runtime.seen(r.userData.name, r.visible); } });
              var found = roots.filter(function (r) { return r.visible; }).map(function (r) { return r.userData.name; });
              var line = found.length ? 'Found: ' + found.join(', ') : 'Point the camera at the marker.';
              if (line !== lastLine) { say(line); lastLine = line; }
              C.runtime.update(dt, t);
              nodes.forEach(function (n) { if (!n.userData.go || !n.userData.go.dead) C.animate(n, t); });
              players.forEach(function (p) { p.update(dt); });
              arScene.renderOn(renderer);
              requestAnimationFrame(tick);
            };
            tick();
          }
        });
        delete window.ARThreeOnLoad;
      };
      if (window.ARController && ARController.getUserMediaThreeScene) window.ARThreeOnLoad();
    });
  }

  // ---- Pictures and faces: MindAR (three 0.160, ES modules: the page hands in what it imported)
  function startMind(lib) {
    if (fromDisk()) return;
    var THREE = lib.THREE, faces = lib.mode === 'face';
    say('Loading the scene...');
    withScene(function (proj) {
      document.title = proj.title;
      var C = core(THREE, lib.GLTFLoader);
      var anchors = proj.markers;
      var go = function (imageTargetSrc) {
        say('Starting the camera...');
        var opts = { container: document.body, uiLoading: 'no', uiScanning: 'no', uiError: 'no' };
        if (!faces) opts.imageTargetSrc = imageTargetSrc;
        var mind = new lib.MindARThree(opts);
        var renderer = mind.renderer, scene = mind.scene, camera = mind.camera;
        C.runtime.setCamera(camera, renderer.domElement);
        var nodes = [], players = [], found = {}, faceAnchors = [];
        // MindAR's face units: about 0.6 of them make a face's width; the editor's face is 1 wide
        var FACE = 0.6;
        // the face itself hides what's behind it (a hat's back, glasses' arms): an invisible occluder
        var faceMesh = null;
        if (faces) {
          faceMesh = mind.addFaceMesh();
          faceMesh.material = new THREE.MeshBasicMaterial({ colorWrite: false });
          faceMesh.renderOrder = -1;
          scene.add(faceMesh);
        }
        anchors.forEach(function (m, i) {
          var a = mind.addAnchor(faces ? (m.facePoint || 1) : i);
          var root = C.anchorWorld(m, nodes, players);
          if (!faces) root.rotation.x = Math.PI / 2;   // the picture lies in XY, Z out of it: the editor's Y-up on it
          else { root.scale.setScalar(FACE); faceAnchors.push({ a: a, name: m.name }); }
          a.group.add(root);
          a.onTargetFound = function () { found[m.name] = true; C.runtime.seen(m.name, true); };
          a.onTargetLost = function () { delete found[m.name]; C.runtime.seen(m.name, false); };
        });
        var clock = new THREE.Clock(), t = 0, lastLine = '';
        mind.start().then(function () {
          renderer.setAnimationLoop(function () {
            var dt = clock.getDelta(); t += dt;
            // a face anchor shows while the face is seen
            faceAnchors.forEach(function (f) {
              var on = f.a.group.visible;
              if (on !== !!found[f.name]) C.runtime.seen(f.name, on);
              if (on) found[f.name] = true; else delete found[f.name];
            });
            var names = Object.keys(found);
            var line = names.length ? 'Found: ' + names.join(', ') : (faces ? 'Look at the camera.' : 'Point the camera at the picture.');
            if (line !== lastLine) { say(line); lastLine = line; }
            C.runtime.update(dt, t);
            nodes.forEach(function (n) { if (!n.userData.go || !n.userData.go.dead) C.animate(n, t); });
            players.forEach(function (p) { p.update(dt); });
            renderer.render(scene, camera);
          });
        }).catch(function (e) { say('The camera didn\'t start: ' + (e && (e.message || e.name) || e)); });
      };
      if (faces) { go(null); return; }
      // the pictures to look for: learnt here, the first time the page opens (MindAR's compiler)
      say('Learning the pictures...');
      Promise.all(anchors.map(function (m) {
        return new Promise(function (ok, bad) { var im = new Image(); im.onload = function () { ok(im); }; im.onerror = function () { bad(new Error('No picture ' + m.target)); }; im.src = m.target; });
      })).then(function (images) {
        var compiler = new lib.Compiler();
        return compiler.compileImageTargets(images, function (p) { say('Learning the pictures... ' + Math.round(p) + '%'); }).then(function () {
          return compiler.exportData();
        });
      }).then(function (data) { go(URL.createObjectURL(new Blob([data]))); })
        .catch(function (e) { say('Problem: ' + (e && e.message || e)); });
    });
  }

  window.Diorama = { say: say, start: startMarkers, startMarkers: startMarkers, startMind: startMind };
})();
