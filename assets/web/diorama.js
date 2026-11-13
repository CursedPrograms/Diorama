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
      node.userData.o = o;
      return node;
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
        if (!c.material || node.userData.rays) return;
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
      });
      return root;
    }
    return { anchorWorld: anchorWorld, animate: animate };
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
              var found = roots.filter(function (r) { return r.visible; }).map(function (r) { return r.userData.name; });
              var line = found.length ? 'Found: ' + found.join(', ') : 'Point the camera at the marker.';
              if (line !== lastLine) { say(line); lastLine = line; }
              nodes.forEach(function (n) { C.animate(n, t); });
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
          a.onTargetFound = function () { found[m.name] = true; };
          a.onTargetLost = function () { delete found[m.name]; };
        });
        var clock = new THREE.Clock(), t = 0, lastLine = '';
        mind.start().then(function () {
          renderer.setAnimationLoop(function () {
            var dt = clock.getDelta(); t += dt;
            // a face anchor shows while the face is seen
            faceAnchors.forEach(function (f) { if (f.a.group.visible) found[f.name] = true; else delete found[f.name]; });
            var names = Object.keys(found);
            var line = names.length ? 'Found: ' + names.join(', ') : (faces ? 'Look at the camera.' : 'Point the camera at the picture.');
            if (line !== lastLine) { say(line); lastLine = line; }
            nodes.forEach(function (n) { C.animate(n, t); });
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
