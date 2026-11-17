// Diorama's player: builds a Diorama scene (scene.json / scene.js) in a phone's browser with three.js, on what the game
// tracks: printed markers (jsartoolkit5), pictures (MindAR image tracking) or the player's face (MindAR face tracking).
// The scene building is the same for all of them (and works with the three.js each one uses); only the starters
// differ. Anchor space as in the editor: Y up, +Z towards the viewer; a marker or picture is 1 across, a face is 1 wide.
(function () {
  'use strict';
  var D2R = Math.PI / 180;
  var HERE = (document.currentScript && document.currentScript.src) || window.location.href;   // lib/diorama.js: Jolt is beside it (lib/jolt)
  // jsartoolkit5 swaps WebAssembly.instantiate for a shim of its own (loaded after this): Jolt gets the real one back
  var WASM_INSTANTIATE = window.WebAssembly && WebAssembly.instantiate;

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

  // the game's renderer (Build Settings > Graphics): three's WebGLRenderer, or its WebGPURenderer (WebGPU where the
  // browser has it, else WebGL 2 by itself). WebXR (Surfaces' AR) needs WebGL, so AR sessions always get WebGL.
  function makeRenderer(lib, opts) {
    var THREE = lib.THREE;
    if (lib.webgpu && THREE.WebGPURenderer) return new THREE.WebGPURenderer(opts);
    return new THREE.WebGLRenderer(opts);
  }
  function usesWebGPU(renderer) { return !!(renderer && renderer.backend && renderer.backend.isWebGPUBackend); }

  // ---------- ArNav: surfaces and paths, the same maths as the editor's (src/arnav.cpp), line for line ----------
  var ArNav = (function () {
    function dist(a, b) { var dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z; return Math.sqrt(dx * dx + dy * dy + dz * dz); }
    function yawOf(dx, dz) { return Math.atan2(-dx, -dz); }   // -Z turned by yaw points along (dx, dz)
    function fromFlat(xyz) { var out = []; for (var i = 0; i + 2 < xyz.length; i += 3) out.push({ x: xyz[i], y: xyz[i + 1], z: xyz[i + 2] }); return out; }
    function length(pts, loop) {
      var L = 0; for (var i = 1; i < pts.length; ++i) L += dist(pts[i - 1], pts[i]);
      if (loop && pts.length > 2) L += dist(pts[pts.length - 1], pts[0]);
      return L;
    }
    function at(pts, s, loop) {
      var r = { p: { x: 0, y: 0, z: 0 }, yaw: 0 };
      if (!pts.length) return r;
      if (pts.length === 1) { r.p = pts[0]; return r; }
      var n = pts.length, segs = loop && n > 2 ? n : n - 1, L = length(pts, loop);
      if (L <= 1e-6) { r.p = pts[0]; return r; }
      if (loop) { s = s % L; if (s < 0) s += L; } else s = Math.min(Math.max(s, 0), L);
      for (var i = 0; i < segs; ++i) {
        var a = pts[i], b = pts[(i + 1) % n], d = dist(a, b);
        if (s <= d || i + 1 === segs) {
          var f = d > 1e-6 ? Math.min(Math.max(s / d, 0), 1) : 0;
          r.p = { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f, z: a.z + (b.z - a.z) * f };
          r.yaw = yawOf(b.x - a.x, b.z - a.z);
          return r;
        }
        s -= d;
      }
      r.p = pts[n - 1]; return r;
    }
    function arrows(pts, spacing, loop) {
      var out = [], L = length(pts, loop);
      if (spacing <= 0.01 || L <= 0) return out;
      for (var s = spacing * 0.5; s < L && out.length < 2000; s += spacing) out.push(at(pts, s, loop));
      return out;
    }
    function resample(pts, spacing) {
      var out = []; if (!pts.length) return out;
      out.push(pts[0]);
      for (var i = 1; i < pts.length; ++i) if (dist(out[out.length - 1], pts[i]) >= spacing) out.push(pts[i]);
      if (pts.length > 1 && dist(out[out.length - 1], pts[pts.length - 1]) > 1e-4) out.push(pts[pts.length - 1]);
      return out;
    }
    function inside(poly, x, z) {
      var inn = false;
      for (var i = 0, j = poly.length - 1; i < poly.length; j = i++) {
        var xi = poly[i][0], zi = poly[i][1], xj = poly[j][0], zj = poly[j][1];
        if (((zi > z) !== (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi)) inn = !inn;
      }
      return inn;
    }
    function dots(poly, spacing) {
      var out = []; if (poly.length < 3 || spacing <= 0.005) return out;
      var x0 = poly[0][0], x1 = x0, z0 = poly[0][1], z1 = z0;
      poly.forEach(function (q) { x0 = Math.min(x0, q[0]); x1 = Math.max(x1, q[0]); z0 = Math.min(z0, q[1]); z1 = Math.max(z1, q[1]); });
      var rowH = spacing * 0.8660254, row = 0;
      for (var z = Math.floor(z0 / rowH) * rowH; z <= z1 && out.length < 20000; z += rowH, ++row) {
        var off = (row & 1) ? spacing * 0.5 : 0;
        for (var x = Math.floor(x0 / spacing) * spacing + off; x <= x1; x += spacing) if (inside(poly, x, z)) out.push([x, z]);
      }
      return out;
    }
    function fan(poly) { var idx = []; for (var i = 1; i + 1 < poly.length; ++i) idx.push(0, i, i + 1); return idx; }
    // the editor's pretend room (floor, table top, wall): the planes the desktop preview "finds"
    var simPlanes = [
      { id: 'floor', alignment: 'horizontal', position: [0, 0, 0], rotX: 0,
        poly: [[-1.6, -2.3], [1.2, -2.4], [2.3, -1.0], [2.2, 1.3], [0.6, 2.2], [-1.4, 2.0], [-2.3, 0.4]] },
      { id: 'table', alignment: 'horizontal', position: [1.45, 0.751, -1.2], rotX: 0, poly: [[-0.48, -0.3], [0.48, -0.3], [0.48, 0.3], [-0.48, 0.3]] },
      { id: 'wall', alignment: 'vertical', position: [0, 1.2, -2.59], rotX: Math.PI / 2, poly: [[-2.0, -1.1], [2.0, -1.1], [2.0, 1.1], [-2.0, 1.1]] }];
    return { fromFlat: fromFlat, length: length, at: at, arrows: arrows, resample: resample, inside: inside, dots: dots, fan: fan, simPlanes: simPlanes };
  })();

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

    // a colour as picked (sRGB, like the editor's); three.js works in linear light
    function rgb(c) { var k = new THREE.Color(); if (THREE.SRGBColorSpace) k.setRGB(c[0], c[1], c[2], THREE.SRGBColorSpace); else k.setRGB(c[0], c[1], c[2]); return k; }
    // Graphics (Build Settings): shadows, bloom, tone mapping, exposure
    var GFX = { shadows: true, quality: 1, bloom: true, bloomStrength: 0.6, bloomThreshold: 0.9, tone: 1, exposure: 1, tracking: 'markers' };
    function graphics(proj) {
      GFX.shadows = proj.shadows !== false; GFX.quality = proj.shadowQuality === undefined ? 1 : proj.shadowQuality;
      GFX.bloom = proj.bloom !== false; GFX.bloomStrength = proj.bloomStrength === undefined ? 0.6 : proj.bloomStrength;
      GFX.bloomThreshold = proj.bloomThreshold === undefined ? 0.9 : proj.bloomThreshold;
      GFX.tone = proj.toneMapping === undefined ? 1 : proj.toneMapping; GFX.exposure = proj.exposure || 1; GFX.tracking = proj.tracking || 'markers';
      GFX.webgpu = proj.renderer === 1;
    }
    function setupRenderer(renderer) {
      renderer.toneMapping = GFX.tone === 2 ? THREE.ACESFilmicToneMapping : GFX.tone === 1 ? (THREE.NeutralToneMapping || THREE.ACESFilmicToneMapping) : THREE.NoToneMapping;
      renderer.toneMappingExposure = GFX.exposure;
      // soft shadows; three's WebGPU renderer filters with PCF (its soft filter isn't ready there)
      if (renderer.shadowMap) { renderer.shadowMap.enabled = GFX.shadows; renderer.shadowMap.type = renderer.isWebGPURenderer ? THREE.PCFShadowMap : THREE.PCFSoftShadowMap; }
    }
    function shadowMapSize() { return GFX.quality === 0 ? 1024 : GFX.quality === 2 ? 4096 : 2048; }
    // bloom: three's UnrealBloom, on WebGL (not in AR sessions); with the camera's picture behind, only what's brighter
    // than paper white blooms (lit and glowing things, never the picture)
    function makeComposer(renderer, scene, camera, cameraPicture) {
      if (!GFX.bloom || !(GFX.bloomStrength > 0) || !renderer.isWebGLRenderer) return Promise.resolve(null);
      return Promise.all([import('three/addons/postprocessing/EffectComposer.js'), import('three/addons/postprocessing/RenderPass.js'),
                          import('three/addons/postprocessing/UnrealBloomPass.js'), import('three/addons/postprocessing/OutputPass.js')]).then(function (m) {
        var size = renderer.getSize(new THREE.Vector2());
        var c = new m[0].EffectComposer(renderer);
        c.addPass(new m[1].RenderPass(scene, camera));
        c.addPass(new m[2].UnrealBloomPass(size, GFX.bloomStrength, 0.4, cameraPicture ? Math.max(1.0, GFX.bloomThreshold) : GFX.bloomThreshold));
        c.addPass(new m[3].OutputPass());
        return c;
      }).catch(function (e) { console.log('Diorama: no bloom (' + (e && e.message || e) + ')'); return null; });
    }
    // what metals reflect: a soft sky over the ground in the anchor's ambient colour (three's metals reflect only this)
    function environment(scene, ambient) {
      var a = ambient || [0.5, 0.5, 0.5], cv = document.createElement('canvas'); cv.width = 64; cv.height = 32;
      var g = cv.getContext('2d'), grad = g.createLinearGradient(0, 0, 0, 32);
      var css = function (k) { return 'rgb(' + [0, 1, 2].map(function (i) { return Math.round(Math.min(1, a[i] * k) * 255); }).join(',') + ')'; };
      grad.addColorStop(0, css(1.9)); grad.addColorStop(0.48, css(1.3)); grad.addColorStop(0.52, css(0.8)); grad.addColorStop(1, css(0.45));
      g.fillStyle = grad; g.fillRect(0, 0, 64, 32);
      var t = new THREE.CanvasTexture(cv); t.mapping = THREE.EquirectangularReflectionMapping; if (THREE.SRGBColorSpace) t.colorSpace = THREE.SRGBColorSpace;
      scene.environment = t;
      if ('environmentIntensity' in scene) scene.environmentIntensity = 0.6;
    }
    function shadowFlags(root, o) {   // what casts and gets shadows
      root.traverse(function (c) {
        if (!c.isMesh || c.isInstancedMesh || c.userData.isPathLook) return;
        c.castShadow = o.castShadows !== false && !o.glow && !o.occluder;
        c.receiveShadow = o.receiveShadows !== false && !o.occluder;
      });
    }
    function material(o, map) {
      var opts = { color: rgb(o.colour), opacity: o.colour[3], side: THREE.DoubleSide, transparent: true, alphaTest: 0.01 };
      if (map) opts.map = map;
      if (o.glow) { opts.blending = THREE.AdditiveBlending; opts.depthWrite = false; }
      if (o.emissiveStrength > 0 && o.emissive) { opts.emissive = rgb(o.emissive); opts.emissiveIntensity = o.emissiveStrength; }
      if (!o.lit) { var b = new THREE.MeshBasicMaterial(opts); if (opts.emissive) b.color.add(opts.emissive.clone().multiplyScalar(opts.emissiveIntensity)); return b; }
      opts.metalness = o.metallic || 0; opts.roughness = o.roughness === undefined ? 0.6 : o.roughness;   // PBR, as the editor draws it
      return new THREE.MeshStandardMaterial(opts);
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
          color: rgb(o.colour), blending: o.glow ? THREE.AdditiveBlending : THREE.NormalBlending }));
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
          shadowFlags(g.scene, o);
          g.scene.traverse(function (c) {
            if (c.isMesh && c.material) {
              var ms = Array.isArray(c.material) ? c.material : [c.material];
              ms.forEach(function (m) {
                if (m.color) m.color.multiply(rgb(o.colour));
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
          var rm = new THREE.Mesh(rg, new THREE.MeshBasicMaterial({ map: rayTex, color: rgb(o.colour),
            transparent: true, opacity: o.colour[3], blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
          node.userData.rays.push(rm); node.add(rm);
        }
      }
      components(o, node);
      shadowFlags(node, o);
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
    // ---------- the game's UI: Canvases and their elements as HTML over the camera ----------
    // laid out on a 400 x 711 phone (as in the editor), scaled to the screen by one CSS variable (--s)
    var uiRoot = null;
    function uiLayer() {
      if (uiRoot) return uiRoot;
      var st = document.createElement('style');
      st.textContent = '#dio-ui{position:fixed;left:0;top:0;right:0;bottom:0;pointer-events:none;z-index:20;font-family:"Segoe UI",Roboto,Arial,sans-serif;--s:1}' +
        '.dio-canvas{position:absolute;left:0;top:0;right:0;bottom:0}' +
        '.dio-ui{position:absolute;box-sizing:border-box;margin:0;padding:0}' +
        '.dio-text{display:flex;align-items:center;white-space:pre-wrap;pointer-events:none;line-height:1.15}' +
        '.dio-button{border:0;cursor:pointer;pointer-events:auto;font:inherit;border-radius:calc(var(--s)*6px)}' +
        '.dio-button:active{filter:brightness(0.85)}' +
        '.dio-slider{pointer-events:auto;cursor:pointer}' +
        '.dio-toggle{display:flex;align-items:center;gap:calc(var(--s)*8px);pointer-events:auto;cursor:pointer}' +
        '.dio-toggle input{width:1.2em;height:1.2em;margin:0;cursor:pointer}' +
        '.dio-panel{pointer-events:auto;border-radius:calc(var(--s)*6px)}' +
        '.dio-image{object-fit:contain;pointer-events:none}';
      document.head.appendChild(st);
      uiRoot = document.createElement('div'); uiRoot.id = 'dio-ui';
      document.body.appendChild(uiRoot);
      var fit = function () { uiRoot.style.setProperty('--s', Math.min(window.innerWidth / 400, window.innerHeight / 711)); };
      window.addEventListener('resize', fit); fit();
      return uiRoot;
    }
    function css(c) { return 'rgba(' + Math.round(c[0] * 255) + ',' + Math.round(c[1] * 255) + ',' + Math.round(c[2] * 255) + ',' + c[3] + ')'; }
    function uiPlace(el, o) {   // where it sits on its parent: anchor + offset, its size
      var ax = o.uiAnchor % 3, ay = Math.floor(o.uiAnchor / 3), r = o.uiRect;
      var key = o.uiAnchor + ',' + r.join(',') + ',' + o.uiFont;
      if (el.userData === key) return; el.userData = key;
      el.style.left = 'calc(' + (ax * 50) + '% + var(--s) * ' + r[0] + 'px)';
      el.style.top = 'calc(' + (ay * 50) + '% + var(--s) * ' + r[1] + 'px)';
      el.style.width = 'calc(var(--s) * ' + r[2] + 'px)'; el.style.height = 'calc(var(--s) * ' + r[3] + 'px)';
      el.style.transform = 'translate(' + (-ax * 50) + '%,' + (-ay * 50) + '%)';
      el.style.fontSize = 'calc(var(--s) * ' + o.uiFont + 'px)';
    }
    var cssLinked = {};
    function uiElement(o, node) {
      var el;
      if (o.hasCanvas) {
        el = document.createElement('div'); el.className = 'dio-canvas ' + (o.uiClass || '');
        if (o.canvasCss && !cssLinked[o.canvasCss]) { cssLinked[o.canvasCss] = 1; var l = document.createElement('link'); l.rel = 'stylesheet'; l.href = o.canvasCss; document.head.appendChild(l); }
        return el;
      }
      var fire = function (fn, arg) { var g = node.userData.go; if (!g) return; g.scripts.forEach(function (sc) { call(g, sc, fn, arg); }); (g.uiListeners && g.uiListeners[fn] || []).forEach(function (f) { try { f(arg); } catch (e) { report('error', '', 0, e.message); } }); };
      switch (o.ui) {
      case 'Text': el = document.createElement('div'); el.textContent = o.uiText; el.style.justifyContent = ['flex-start', 'center', 'flex-end'][o.uiAlign] || 'center'; el.style.color = css(o.uiColour); break;
      case 'Button': el = document.createElement('button'); el.textContent = o.uiText; el.style.background = css(o.uiBg); el.style.color = css(o.uiColour);
        el.addEventListener('click', function (ev) {
          ev.stopPropagation(); unlockMedia(); fire('onClick');
          if (o.uiOnClick) try { new Function('nav', 'find', 'loadScene', 'log', 'button', o.uiOnClick)(nav, function (p) { return find(p); }, loadScene, function () { console.log.apply(console, arguments); }, node.userData.go); }
            catch (e) { report('error', o.name + ' On Click', 0, e.message); }
        }); break;
      case 'Slider': el = document.createElement('input'); el.type = 'range'; el.min = o.uiMin; el.max = o.uiMax; el.step = 'any'; el.value = o.uiValue; el.style.accentColor = css(o.uiBg);
        el.addEventListener('input', function () { o.uiValue = +el.value; fire('onChange', o.uiValue); }); break;
      case 'Toggle': el = document.createElement('label'); var box = document.createElement('input'); box.type = 'checkbox'; box.checked = !!o.uiChecked; box.style.accentColor = css(o.uiBg);
        var sp = document.createElement('span'); sp.textContent = o.uiText; el.appendChild(box); el.appendChild(sp); el.style.color = css(o.uiColour);
        box.addEventListener('change', function () { o.uiChecked = box.checked; fire('onChange', box.checked); }); el.box = box; el.label = sp; break;
      case 'Image': el = document.createElement('img'); if (o.uiFile) el.src = o.uiFile; el.style.opacity = o.uiColour[3]; break;
      case 'Panel': el = document.createElement('div'); el.style.background = css(o.uiBg); break;
      default: return null;
      }
      el.className = 'dio-ui dio-' + o.ui.toLowerCase() + ' ' + (o.uiClass || '');
      el.dataset.name = o.name;
      ['mousedown', 'touchstart'].forEach(function (k) { el.addEventListener(k, function (ev) { ev.stopPropagation(); }, { passive: true }); });   // not a tap on the 3D
      uiPlace(el, o);
      return el;
    }
    function uiVisible(node) {   // on, active, and (for a Canvas tied to its anchor) the anchor seen
      var n = node;
      for (; n && n.userData && n.userData.o; n = n.parent) {
        if (n.visible === false) return false;
        var uo = n.userData.o; if (uo.ui && uo.uiOn === false) return false;
      }
      if (!withAnchorUnder(node)) return true;   // screen UI: whatever the tracking does
      for (; n; n = n.parent) if (n.visible === false) return false;
      return true;
    }
    function withAnchorUnder(node) { for (var n = node; n && n.userData && n.userData.o; n = n.parent) if (n.userData.o.hasCanvas) return n.userData.o.canvasWithAnchor; return false; }
    function animateUI(node) {
      var el = node.userData.el, o = node.userData.o; if (!el) return;
      if (!el.parentNode) {   // its place: in its parent element, or on the screen
        var pe = node.parent && node.parent.userData && node.parent.userData.el;
        (pe || uiLayer()).appendChild(el);
      }
      if (!o.hasCanvas) uiPlace(el, o);
      var vis = uiVisible(node);
      if (el.shown !== vis) { el.shown = vis; el.style.display = vis ? '' : 'none'; }
    }

    // lights fade to nothing at their Range (decay 0: no inverse square), as the editor draws them
    function components(o, node) {
      if (o.hasCanvas || o.ui) { var ue = uiElement(o, node); if (ue) node.userData.el = ue; }
      if (o.occluder) occlude(node);
      if (o.hasLight) {
        var col = rgb(o.lightColour), li;
        if (o.lightType === 1) li = new THREE.SpotLight(col, o.lightIntensity * K, o.lightRange, o.spotAngle * D2R / 2, 0.15, 0);
        else if (o.lightType === 2) li = new THREE.DirectionalLight(col, o.lightIntensity * K);
        else li = new THREE.PointLight(col, o.lightIntensity * K, o.lightRange, 0);
        if (li.target) { var tg = new THREE.Object3D(); tg.position.set(0, 0, -1); node.add(tg); li.target = tg; }
        if (o.lightType === 1 && o.lightShadows && GFX.shadows) { li.castShadow = true; li.shadow.mapSize.set(1024, 1024); li.shadow.bias = -0.0005; li.shadow.normalBias = 0.01; }
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
        var vt;
        if (o.videoAlpha === 2) { var key = chromaKey(v, o); vt = srgb(key.texture); node.userData.chroma = key; }
        else vt = srgb(new THREE.VideoTexture(v));
        node.traverse(function (c) {
          if ((c.isMesh || c.isSprite) && c.material && !o.occluder) {
            c.material.map = vt;
            if (o.videoAlpha) { c.material.transparent = true; c.material.alphaTest = 0.02; c.material.depthWrite = false; }
            c.material.needsUpdate = true;
          }
        });
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
      if (o.hasPath) pathLook(node, o);
      if (o.hasFollower) node.userData.follow = { s: 0, walking: o.followOnStart !== false, path: o.followPath };
    }
    // ---------- AR Path (path marking: arrows along it, its line) and AR Path Follower (path following) ----------
    var arrowGeo = null;
    function arrowGeometry() {   // the editor's chevron: along -Z, 1 long, a little thick
      if (arrowGeo) return arrowGeo;
      var sh = new THREE.Shape(), pts = [[0, -0.5], [0.4, 0.35], [0, 0.12], [-0.4, 0.35]];
      sh.moveTo(pts[0][0], -pts[0][1]); for (var i = 1; i < 4; ++i) sh.lineTo(pts[i][0], -pts[i][1]); sh.lineTo(pts[0][0], -pts[0][1]);
      arrowGeo = new THREE.ExtrudeGeometry(sh, { depth: 0.06, bevelEnabled: false });
      arrowGeo.rotateX(-Math.PI / 2);
      return arrowGeo;
    }
    function pathLook(node, o) {   // (again, when its points change)
      if (node.userData.pathLook) node.remove(node.userData.pathLook);
      var g = new THREE.Group(); g.userData.isPathLook = true;
      var pts = ArNav.fromFlat(o.pathPoints || []), c = o.pathColour || [0.15, 0.2, 0.75, 0.95];
      var mat = new THREE.MeshLambertMaterial({ color: rgb(c), transparent: c[3] < 1, opacity: c[3] });
      ArNav.arrows(pts, o.arrowSpacing || 0.5, !!o.pathLoop).forEach(function (a) {
        var m = new THREE.Mesh(arrowGeometry(), mat);
        m.position.set(a.p.x, a.p.y + 0.01, a.p.z); m.rotation.y = a.yaw; m.scale.setScalar(o.arrowSize || 0.25);
        g.add(m);
      });
      if (o.pathLine !== false) for (var i = 0; i + 1 < pts.length + (o.pathLoop && pts.length > 2 ? 1 : 0); ++i) {
        var s = segmentMesh(); s.material.color.setRGB(c[0], c[1], c[2]); s.material.opacity = c[3] * 0.45;
        var p0 = pts[i], p1 = pts[(i + 1) % pts.length];
        placeSegment(s, new THREE.Vector3(p0.x, p0.y + 0.004, p0.z), new THREE.Vector3(p1.x, p1.y + 0.004, p1.z), 0.02);
        g.add(s);
      }
      g.visible = o.pathOn !== false;
      node.add(g); node.userData.pathLook = g;
    }
    function pathOwner(name) {   // the AR Path of that name (in the scene, or walked and saved)
      var hit = null;
      (R.nodes || []).forEach(function (n) { var o = n.userData.o; if (!hit && o && o.hasPath && o.pathName === name && !(n.userData.go && n.userData.go.dead)) hit = n; });
      return hit;
    }
    var mFollow = new THREE.Matrix4(), mTmp = new THREE.Matrix4(), mParentInv = new THREE.Matrix4();
    function follow(node, dt) {   // on its path, facing the way it goes; its own Position is an offset from it
      var o = node.userData.o, f = node.userData.follow;
      if (o.followerOn === false) return;
      var owner = pathOwner(f.path);
      if (!owner) return;
      var po = owner.userData.o, pts = ArNav.fromFlat(po.pathPoints || []);
      if (f.walking) f.s += (o.followSpeed || 0.6) * dt;
      var L = ArNav.length(pts, !!o.followLoop);
      if (!o.followLoop && f.s >= L && f.walking) { f.walking = false; f.s = L; if (node.userData.go) node.userData.go.scripts.forEach(function (sc) { call(node.userData.go, sc, 'onArrived', f.path); }); }
      var pose = ArNav.at(pts, f.s, !!o.followLoop);
      owner.updateWorldMatrix(true, false);
      node.updateMatrix();
      mFollow.copy(owner.matrixWorld).multiply(mTmp.makeTranslation(pose.p.x, pose.p.y, pose.p.z)).multiply(mTmp.makeRotationY(pose.yaw)).multiply(node.matrix);
      if (node.parent) { node.parent.updateWorldMatrix(true, false); mParentInv.copy(node.parent.matrixWorld); if (mParentInv.invert) mParentInv.invert(); else mParentInv.getInverse(node.parent.matrixWorld); mFollow.premultiply(mParentInv); }
      mFollow.decompose(node.position, node.quaternion, node.scale);
    }
    // nav: record a walked path, save it, have the guides follow one (path marking and following, from scripts and buttons)
    var NAV = { recording: null, points: [], lastDt: 0 };
    function navRoot() { var k = Object.keys(R.roots); return k.length ? R.roots[k[0]] : null; }
    function storeKey(name) { return 'dio-path:' + (document.title || '') + ':' + name; }
    function setPath(name, ptsRoot) {   // a path (in its anchor's space) onto the AR Paths of that name, or a new one
      var root = navRoot(); if (!root) return;
      var owner = pathOwner(name), v = new THREE.Vector3();
      if (!owner) {
        var data = { id: ++R.nextId, parent: -1, name: name, kind: 'Empty', active: true, position: [0, 0, 0], rotation: [0, 0, 0], scale: [1, 1, 1], colour: [1, 1, 1, 1],
          anim: 'Still', animSpeed: 1, animAmount: 0, scripts: [], hasPath: true, pathOn: true, pathName: name, pathPoints: [], arrowSpacing: 0.5, arrowSize: 0.25,
          pathColour: [0.15, 0.2, 0.75, 0.95], pathLine: true, pathLoop: false };
        owner = build(data, R.players); root.add(owner); R.nodes.push(owner); attachScripts(owner);
      }
      owner.updateWorldMatrix(true, false); root.updateWorldMatrix(true, false);
      var flat = [];
      ptsRoot.forEach(function (q) { v.set(q.x, q.y, q.z); root.localToWorld(v); owner.worldToLocal(v); flat.push(v.x, v.y, v.z); });
      owner.userData.o.pathPoints = flat;
      pathLook(owner, owner.userData.o);
    }
    var nav = {
      get recording() { return NAV.recording; },
      record: function (name) { NAV.recording = name || 'Path 1'; NAV.points = []; say('Recording ' + NAV.recording + ': walk the way'); },
      stop: function () { var pts = ArNav.resample(NAV.points, 0.25); NAV.recording = null; return pts; },
      save: function (name) {
        var n = name || NAV.recording || 'Path 1', pts = NAV.recording ? nav.stop() : ArNav.resample(NAV.points, 0.25);
        if (pts.length < 2) { say('Walk a little first'); return false; }
        try { localStorage.setItem(storeKey(n), JSON.stringify(pts)); } catch (e) {}
        setPath(n, pts); say('Saved ' + n + ' (' + pts.length + ' points)'); return true;
      },
      toggle: function (name) { if (NAV.recording === name) { nav.save(name); return 'saved'; } nav.record(name); return 'recording'; },
      follow: function (name) {
        (R.nodes || []).forEach(function (n) { var f = n.userData.follow; if (f) { f.path = name; f.s = 0; f.walking = true; } });
        if (!pathOwner(name)) say('No ' + name + ' yet: save it first');
      },
      stopFollowing: function () { (R.nodes || []).forEach(function (n) { if (n.userData.follow) n.userData.follow.walking = false; }); },
      reset: function () {
        NAV.recording = null; NAV.points = [];
        (R.nodes || []).forEach(function (n) { var f = n.userData.follow; if (f) { f.s = 0; f.walking = false; } });
        say('Reset');
      },
      paths: function () { var names = []; (R.nodes || []).forEach(function (n) { var o = n.userData.o; if (o && o.hasPath && names.indexOf(o.pathName) < 0) names.push(o.pathName); }); return names; },
      points: function (name) { var o = pathOwner(name); return o ? ArNav.fromFlat(o.userData.o.pathPoints) : []; },
      forget: function (name) { try { localStorage.removeItem(storeKey(name)); } catch (e) {} }
    };
    function navSample() {   // while recording: where the phone is, on the floor (its anchor's space)
      if (!NAV.recording || !R.camera) return;
      var root = navRoot(); if (!root || !root.visible) return;
      var p = new THREE.Vector3(); R.camera.getWorldPosition(p); root.worldToLocal(p); p.y = 0;
      var last = NAV.points[NAV.points.length - 1];
      if (!last || Math.hypot(p.x - last.x, p.z - last.z) > 0.08) NAV.points.push({ x: p.x, y: 0, z: p.z });
    }
    function navLoadSaved() {   // paths walked and saved before: in their place
      nav.paths().forEach(function (name) {
        try { var j = localStorage.getItem(storeKey(name)); if (j) setPath(name, JSON.parse(j)); } catch (e) {}
      });
    }
    // a green screen keyed out: the video drawn through a small shader into a canvas of its own (its texture), each frame
    function chromaKey(video, o) {
      var cv = document.createElement('canvas'); cv.width = 2; cv.height = 2;
      var gl = cv.getContext('webgl2', { premultipliedAlpha: false, preserveDrawingBuffer: true, alpha: true });
      var tex = new THREE.CanvasTexture(cv);
      if (!gl) { tex.image = video; return { texture: tex, update: function () { tex.needsUpdate = true; } }; }   // no WebGL 2: unkeyed
      var sh = function (type, src) { var x = gl.createShader(type); gl.shaderSource(x, src); gl.compileShader(x); return x; };
      var pr = gl.createProgram();
      gl.attachShader(pr, sh(gl.VERTEX_SHADER, '#version 300 es\nin vec2 p; out vec2 uv; void main(){ uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); gl_Position = vec4(p, 0.0, 1.0); }'));
      gl.attachShader(pr, sh(gl.FRAGMENT_SHADER, '#version 300 es\nprecision mediump float; in vec2 uv; uniform sampler2D v; uniform vec3 key; uniform float tol, soft; out vec4 o;\n' +
        'vec2 cbcr(vec3 c){ return vec2(-0.1687*c.r - 0.3313*c.g + 0.5*c.b, 0.5*c.r - 0.4187*c.g - 0.0813*c.b); }\n' +
        'void main(){ vec4 c = texture(v, uv); float d = distance(cbcr(c.rgb), cbcr(key)) * 1.4;\n' +
        '  float a = smoothstep(tol, tol + max(soft, 0.001), d);\n' +
        '  float spill = max(0.0, dot(c.rgb, key) / max(dot(key, key), 0.001) - 0.5 * dot(c.rgb, vec3(1.0) - key) / max(dot(vec3(1.0) - key, vec3(1.0) - key), 0.001));\n' +
        '  vec3 rgb = c.rgb - key * spill * (1.0 - a) * 0.8;   // the key colour\'s fringe taken out of the edge\n' +
        '  o = vec4(rgb, a * c.a); }'));
      gl.linkProgram(pr); gl.useProgram(pr);
      var buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
      var loc = gl.getAttribLocation(pr, 'p'); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
      var vt = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, vt);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      var k = o.videoKey || [0, 1, 0];
      gl.uniform3f(gl.getUniformLocation(pr, 'key'), k[0], k[1], k[2]);
      gl.uniform1f(gl.getUniformLocation(pr, 'tol'), o.videoKeyTol === undefined ? 0.35 : o.videoKeyTol);
      gl.uniform1f(gl.getUniformLocation(pr, 'soft'), o.videoKeySoft === undefined ? 0.1 : o.videoKeySoft);
      var last = -1;
      return { texture: tex, update: function () {
        if (video.readyState < 2 || video.currentTime === last) return;
        last = video.currentTime;
        var w = Math.min(1280, video.videoWidth || 2), h = Math.round(w * (video.videoHeight || 2) / (video.videoWidth || 2));
        if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; tex.dispose(); }
        gl.viewport(0, 0, w, h);
        gl.bindTexture(gl.TEXTURE_2D, vt);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);
        gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        tex.needsUpdate = true;
      } };
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
    window.DioramaUnlock = function () { unlockMedia(); };
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
      if (node.userData.follow) follow(node, R.dt || 0);
      if (node.userData.chroma) node.userData.chroma.update();
      if (node.userData.pathLook) node.userData.pathLook.visible = o.pathOn !== false;
      if (node.userData.el) animateUI(node);
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
      this._vel = vec([0, 0, 0]);   // moves it by itself; with a Rigidbody, velocity is the body's
      Object.defineProperty(this, 'velocity', { get: function () { return o.hasBody ? rigidbody(self).velocity : self._vel; },
        set: function (v) { var to = self.velocity; to.set(v.x, v.y, v.z); }, enumerable: true });
      Object.defineProperty(this, 'active', { get: function () { return node.visible; }, set: function (v) { node.visible = !!v; } });
      this.scripts = [];
    }
    GameObject.prototype.getComponent = function (type) {
      var o = this.data, node = this.node;
      switch (String(type).toLowerCase()) {
      case 'transform': return this.transform;
      case 'animate': return o.hasAnim ? { get on() { return o.animOn; }, set on(v) { o.animOn = !!v; }, get does() { return o.anim; }, set does(v) { o.anim = v; },
                                            get speed() { return o.animSpeed; }, set speed(v) { o.animSpeed = +v; }, get amount() { return o.animAmount; }, set amount(v) { o.animAmount = +v; } } : null;
      case 'material': return { colour: vec(o.colour, function () { node.traverse(function (c) { if (c.material && c.material.color && !c.material.userData.isPathLook) c.material.color.copy(rgb(o.colour)); }); }), get metallic() { return o.metallic || 0; }, set metallic(v) { o.metallic = +v; node.traverse(function (c) { if (c.material && 'metalness' in c.material) c.material.metalness = +v; }); },
        get roughness() { return o.roughness === undefined ? 0.6 : o.roughness; }, set roughness(v) { o.roughness = +v; node.traverse(function (c) { if (c.material && 'roughness' in c.material) c.material.roughness = +v; }); },
        get emissiveStrength() { return o.emissiveStrength || 0; }, set emissiveStrength(v) { o.emissiveStrength = +v; node.traverse(function (c) { if (c.material && c.material.emissive) { c.material.emissive.copy(rgb(o.emissive || [1, 1, 1])); c.material.emissiveIntensity = +v; } }); },
        get opacity() { return o.colour[3]; }, set opacity(v) { o.colour[3] = +v; node.traverse(function (c) { if (c.material) { c.material.userData.base = +v; } }); } };
      case 'words': return o.kind === 'Words' ? { get text() { return o.text; }, set text(v) { o.text = String(v); rebuild(this.go); }, go: this } : null;
      case 'model': return o.kind === 'Model' ? { get clip() { return o.clip; }, play: function (name) { o.clip = name; rebuild(this.go); }, go: this } : null;
      case 'canvas': return o.hasCanvas ? { get visible() { return node.visible; }, set visible(v) { node.visible = !!v; }, element: node.userData.el } : null;
      case 'text': case 'button': case 'slider': case 'toggle': case 'image': case 'panel':
        if (!o.ui || o.ui.toLowerCase() !== String(type).toLowerCase()) return null;
        var el = node.userData.el, go = this;
        var on = function (fn) { return function (f) { go.uiListeners = go.uiListeners || {}; (go.uiListeners[fn] = go.uiListeners[fn] || []).push(f); }; };
        var c = { element: el, get on() { return o.uiOn !== false; }, set on(v) { o.uiOn = !!v; } };
        Object.defineProperty(c, 'text', { get: function () { return o.uiText; }, set: function (v) { o.uiText = String(v); if (el.label) el.label.textContent = o.uiText; else el.textContent = o.uiText; } });
        Object.defineProperty(c, 'value', { get: function () { return o.uiValue; }, set: function (v) { o.uiValue = +v; el.value = +v; } });
        Object.defineProperty(c, 'isOn', { get: function () { return !!o.uiChecked; }, set: function (v) { o.uiChecked = !!v; if (el.box) el.box.checked = !!v; } });
        Object.defineProperty(c, 'picture', { get: function () { return o.uiFile; }, set: function (v) { o.uiFile = String(v); el.src = o.uiFile; } });
        c.onClick = on('onClick'); c.onChange = on('onChange');
        c.position = vec(o.uiRect); c.colour = vec(o.uiColour, function () { el.style.color = css(o.uiColour); });
        return c;
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
      case 'rigidbody': case 'body': return o.hasBody ? rigidbody(this) : null;
      case 'collider': case 'boxcollider': case 'spherecollider': case 'capsulecollider': case 'meshcollider': return o.hasCollider ? collider(o) : null;
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

    // ---------- physics: Jolt (WASM) under the Rigidbody and Collider components (physics.js; the native player runs
    // the same engine in C++). Loaded only when the game has colliders; things wait where they are until it's ready.
    var PHYS = { world: null, loading: false, failed: false, byId: {} };
    function physicsLists() {   // each anchor's objects, parents first, and whether they're shown
      var proj = window.DIORAMA_PROJECT, lists = [], byId = {};
      (proj.markers || []).forEach(function (m) {
        var root = R.roots[m.name], list = [];
        if (root) root.traverse(function (c) {
          var o = c.userData && c.userData.o; if (!o || (c.userData.go && c.userData.go.dead)) return;
          var on = true; for (var n = c; n && n !== root; n = n.parent) if (n.visible === false) { on = false; break; }
          list.push({ o: o, on: on }); byId[o.id] = c;
        });
        lists.push(list);
      });
      PHYS.byId = byId;
      return lists;
    }
    function modelPoints(o) {   // a model's points in its own space (its mesh collider's hull); null until it's loaded
      var node = PHYS.byId[o.id]; if (!node) return null;
      var pts = [], inv = new THREE.Matrix4(), m = new THREE.Matrix4(), v = new THREE.Vector3();
      node.updateMatrixWorld(true);
      if (inv.invert) inv.copy(node.matrixWorld).invert(); else inv.getInverse(node.matrixWorld);
      node.traverse(function (c) {
        if (!c.isMesh || !c.geometry || !c.geometry.attributes.position || (c.userData && c.userData.o && c !== node)) return;
        m.multiplyMatrices(inv, c.matrixWorld);
        var a = c.geometry.attributes.position;
        for (var i = 0; i < a.count; ++i) { v.fromBufferAttribute(a, i).applyMatrix4(m); pts.push(v.x, v.y, v.z); }
      });
      return pts.length >= 12 ? pts : null;
    }
    function physicsStep(dt, t) {
      var proj = window.DIORAMA_PROJECT;
      if (!proj || PHYS.failed || !window.DioramaPhysics) return;
      if (!PHYS.world) {
        if (PHYS.loading || !window.DioramaPhysics.any(proj)) return;
        PHYS.loading = true;
        var url = new URL('jolt/jolt.js', HERE).href, shim = WebAssembly.instantiate;
        if (WASM_INSTANTIATE) WebAssembly.instantiate = WASM_INSTANTIATE;
        var giveBack = function () { WebAssembly.instantiate = shim; };
        new Function('u', 'return import(u)')(url).then(function (m) { return m.default(); }).then(function (Jolt) {
          giveBack();
          PHYS.world = window.DioramaPhysics.create(Jolt, proj, { modelPoints: modelPoints });
        }).catch(function (e) { giveBack(); PHYS.failed = true; report('error', '', 0, 'Physics could not start: ' + (e && e.message || e)); });
        return;
      }
      PHYS.world.step(dt, t, physicsLists());
      PHYS.world.takeContacts().forEach(function (c) {
        var na = PHYS.byId[c.a], nb = c.b >= 0 ? PHYS.byId[c.b] : null;
        var hook = c.trigger ? (c.enter ? 'onTriggerEnter' : 'onTriggerExit') : (c.enter ? 'onCollisionEnter' : 'onCollisionExit');
        var tell = function (node, otherNode, floor, flip) {
          if (!node) return;
          var g = wrap(node), n = c.normal || [0, 1, 0], k = flip ? -1 : 1, pt = c.point || [0, 0, 0];
          var info = { gameObject: otherNode ? wrap(otherNode) : null, floor: floor, point: { x: pt[0], y: pt[1], z: pt[2] },
                       normal: { x: n[0] * k, y: n[1] * k, z: n[2] * k }, speed: c.speed || 0 };
          if (!g.dead) g.scripts.forEach(function (sc) { call(g, sc, hook, info); });
        };
        tell(na, nb, c.b === -1, false);
        tell(nb, na, false, true);
      });
    }
    function anchorIndexOf(node) {   // which anchor (project.markers) an object is under
      var root = anchorOf(node), proj = window.DIORAMA_PROJECT || { markers: [] };
      for (var i = 0; i < proj.markers.length; ++i) if (R.roots[proj.markers[i].name] === root) return i;
      return -1;
    }
    function xyz(x, y, z) { return x && typeof x === 'object' ? [+x.x || 0, +x.y || 0, +x.z || 0] : [+x || 0, +y || 0, +z || 0]; }
    function liveVec(get, set) {   // x, y, z read and written through functions
      var v = {};
      ['x', 'y', 'z'].forEach(function (k, i) { Object.defineProperty(v, k, { get: function () { return get()[i]; }, set: function (n) { var a = get().slice(); a[i] = +n; set(a); }, enumerable: true }); });
      v.set = function (x, y, z) { set(xyz(x, y, z)); return v; };
      v.toArray = function () { return get().slice(); };
      return v;
    }
    var FORCE_MODES = { force: 0, impulse: 1, acceleration: 2, velocitychange: 3 };
    function rigidbody(go) {   // Unity's Rigidbody, for scripts
      var o = go.data, id = o.id;
      var W = function () { return PHYS.world; };
      var vel = function (angular) {
        return liveVec(function () { return (W() && W().velocity(id, angular)) || [0, 0, 0]; }, function (a) { if (W()) W().setVelocity(id, angular, a); });
      };
      var push = function (torque) {
        return function (x, y, z, mode) {
          if (x && typeof x === 'object') mode = y;
          var m = typeof mode === 'number' ? mode : FORCE_MODES[String(mode || 'force').toLowerCase().replace(/[^a-z]/g, '')] || 0;
          if (W()) W().addForce(id, xyz(x, y, z), m, torque);
        };
      };
      var rb = { addForce: push(false), addTorque: push(true),
        sleep: function () { if (W()) W().wake(id, false); }, wakeUp: function () { if (W()) W().wake(id, true); },
        movePosition: function (x, y, z) { var p = xyz(x, y, z); o.position[0] = p[0]; o.position[1] = p[1]; o.position[2] = p[2]; },
        moveRotation: function (x, y, z) { var r = xyz(x, y, z); o.rotation[0] = r[0]; o.rotation[1] = r[1]; o.rotation[2] = r[2]; } };
      Object.defineProperty(rb, 'velocity', { get: function () { return vel(false); }, set: function (v) { vel(false).set(v); } });
      Object.defineProperty(rb, 'angularVelocity', { get: function () { return vel(true); }, set: function (v) { vel(true).set(v); } });
      Object.defineProperty(rb, 'isSleeping', { get: function () { return !!W() && W().sleeping(id); } });
      Object.defineProperty(rb, 'ready', { get: function () { return !!W(); } });
      [['mass', 'mass', Number], ['drag', 'drag', Number], ['angularDrag', 'angularDrag', Number], ['useGravity', 'useGravity', Boolean],
       ['isKinematic', 'isKinematic', Boolean], ['constraints', 'freeze', Number]].forEach(function (k) {
        Object.defineProperty(rb, k[0], { get: function () { return o[k[1]]; }, set: function (v) { o[k[1]] = k[2](v); }, enumerable: true });
      });
      rb.go = go;
      return rb;
    }
    function collider(o) {
      var c = { get enabled() { return o.colliderOn !== false; }, set enabled(v) { o.colliderOn = !!v; },
        get isTrigger() { return !!o.isTrigger; }, set isTrigger(v) { o.isTrigger = !!v; },
        get friction() { return o.friction; }, set friction(v) { o.friction = +v; },
        get bounciness() { return o.bounciness; }, set bounciness(v) { o.bounciness = +v; },
        get radius() { return o.colRadius; }, set radius(v) { o.colRadius = +v; },
        get height() { return o.colHeight; }, set height(v) { o.colHeight = +v; },
        get shape() { return ['Box', 'Sphere', 'Capsule', 'Mesh'][o.colShape | 0]; },
        size: vec(o.colSize), centre: vec(o.colCentre) };
      c.center = c.centre;
      return c;
    }
    function physicsApi(go) {   // a script's physics: rays in its anchor's space, the gravity
      var api = {
        raycast: function (from, dir, maxDistance) {
          if (!PHYS.world) return null;
          var h = PHYS.world.raycast(anchorIndexOf(go.node), xyz(from), xyz(dir), maxDistance > 0 ? maxDistance : 1e4);
          if (!h) return null;
          var node = h.id >= 0 ? PHYS.byId[h.id] : null;
          return { gameObject: node ? wrap(node) : null, floor: h.floor, point: { x: h.point[0], y: h.point[1], z: h.point[2] },
                   normal: { x: h.normal[0], y: h.normal[1], z: h.normal[2] }, distance: h.distance };
        }
      };
      Object.defineProperty(api, 'gravity', { get: function () { var p = window.DIORAMA_PROJECT || {}; if (!p.gravity) p.gravity = [0, -9.81, 0]; return vec(p.gravity); },
        set: function (v) { var p = window.DIORAMA_PROJECT || {}; p.gravity = xyz(v); } });
      Object.defineProperty(api, 'ready', { get: function () { return !!PHYS.world; } });
      return api;
    }
    function wrap(node) { if (!node.userData.go) node.userData.go = new GameObject(node); return node.userData.go; }
    function rebuild(go) {   // a look that changed (words, a clip): built again in place
      var node = go.node, o = go.data, fresh = build(o, R.players);
      while (node.children.length) { var c = node.children[0]; if (c.userData && c.userData.o) break; node.remove(c); }
      fresh.children.slice().forEach(function (c) { node.add(c); });
      ['sprites', 'rays', 'light', 'audio', 'video', 'particles', 'line', 'trail', 'el', 'pathLook', 'follow', 'chroma'].forEach(function (k) { node.userData[k] = fresh.userData[k]; });
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
      // a path may start with an anchor's name ("Main Camera/Torch", "Hiro/Cube"): only under that one
      if (!from && parts.length > 1 && R.roots[parts[0]]) { roots = [R.roots[parts[0]]]; parts = parts.slice(1); }
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
          var f = new Function('gameObject', 'find', 'spawn', 'destroy', 'log', 'time', 'camera', 'input', 'loadScene', 'scene', 'nav', 'planes', 'physics',
            '"use strict";\n' + code + '\n;return { onCollisionEnter: typeof onCollisionEnter === "function" ? onCollisionEnter : null,' +
            ' onCollisionExit: typeof onCollisionExit === "function" ? onCollisionExit : null, onTriggerEnter: typeof onTriggerEnter === "function" ? onTriggerEnter : null,' +
            ' onTriggerExit: typeof onTriggerExit === "function" ? onTriggerExit : null, start: typeof start === "function" ? start : null, update: typeof update === "function" ? update : null,' +
            ' onTap: typeof onTap === "function" ? onTap : null, onFound: typeof onFound === "function" ? onFound : null, onLost: typeof onLost === "function" ? onLost : null, onClick: typeof onClick === "function" ? onClick : null, onChange: typeof onChange === "function" ? onChange : null, onPlanesChanged: typeof onPlanesChanged === "function" ? onPlanesChanged : null, onArrived: typeof onArrived === "function" ? onArrived : null };\n//# sourceURL=' + sc.file);
          var log = function () { report('log', sc.file, 0, Array.prototype.map.call(arguments, function (a) { return typeof a === 'object' ? JSON.stringify(a) : String(a); }).join(' ')); };
          var api = f.call(go, go, function (p) { return find(p); }, spawn, destroy, log, function () { return R.t; }, R.cameraApi, R.input, loadScene, sceneInfo(), nav, PLANES, physicsApi(go));
          go.scripts.push({ name: name, file: sc.file, fns: api, self: go, started: false });
        } catch (e) { report('error', sc.file, lineOf(e, sc.file), e.message); }
      });
      R.objects.push(go);
    }
    // scenes: each is its own page (scene 0 is index.html); loadScene("Name") or loadScene(1) goes there
    function sceneInfo() {
      var list = window.DIORAMA_SCENES || [{ name: 'Main', page: 'index.html' }], i = window.DIORAMA_SCENE_INDEX || 0;
      return { name: (list[i] || list[0]).name, index: i, count: list.length, names: list.map(function (s) { return s.name; }) };
    }
    function loadScene(which) {
      var list = window.DIORAMA_SCENES || [], s = typeof which === 'number' ? list[which] : list.filter(function (x) { return x.name === which; })[0];
      if (!s) { report('error', '', 0, 'loadScene: no scene ' + JSON.stringify(which)); return; }
      window.location.href = s.page;
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
      if (first && at && at.worldPosition) {   // a place in the world (a hit on a plane): into its parent's space
        var wv = new THREE.Vector3(at.worldPosition.x, at.worldPosition.y, at.worldPosition.z);
        if (first.node.parent) { first.node.parent.updateWorldMatrix(true, false); first.node.parent.worldToLocal(wv); }
        first.position.set(wv.x, wv.y, wv.z);
      }
      return first;
    }
    function destroy(go) {
      if (!go || !go.node) return;
      go.node.traverse(function (c) { if (c.userData.go) c.userData.go.dead = true; if (c.userData.el && c.userData.el.parentNode) c.userData.el.parentNode.removeChild(c.userData.el); });
      if (go.node.parent) go.node.parent.remove(go.node);
      R.nodes = R.nodes.filter(function (n) { return !n.userData.go || !n.userData.go.dead; });
    }
    var PLANES = { list: [], byId: {}, changed: { added: [], updated: [], removed: [] }, look: null, settings: null };
    // a plane found (or changed): its pose (a THREE.Matrix4, world), its boundary in its own space ([[x, z], ...]), horizontal / vertical
    PLANES.set = function (id, matrix, poly, alignment) {
      var st = PLANES.settings || { planeDetect: 2, showPlanes: true, planeColour: [0.25, 0.95, 0.55, 0.35] };
      if ((st.planeDetect === 0 && alignment === 'vertical') || (st.planeDetect === 1 && alignment !== 'vertical')) return;
      var p = PLANES.byId[id], isNew = !p;
      if (isNew) { p = { id: id, alignment: alignment, node: new THREE.Group() }; PLANES.byId[id] = p; PLANES.list.push(p); if (PLANES.look) PLANES.look.add(p.node); }
      p.node.matrixAutoUpdate = false; p.node.matrix.copy(matrix); p.node.matrixWorldNeedsUpdate = true;
      var key = JSON.stringify(poly);
      if (key !== p.key) {   // its look: the fill, the dots, the edge (and the plane prefab, once)
        p.key = key; p.boundary = poly.map(function (q) { return { x: q[0], z: q[1] }; });
        while (p.node.children.length && !p.node.children[0].userData.prefab) p.node.remove(p.node.children[0]);
        if (st.showPlanes !== false && poly.length >= 3) {
          var c = st.planeColour || [0.25, 0.95, 0.55, 0.35];
          var g = new THREE.BufferGeometry(), pos = [];
          poly.forEach(function (q) { pos.push(q[0], 0.004, q[1]); });
          attr(g, 'position', pos, 3); g.setIndex(ArNav.fan(poly));
          p.node.add(new THREE.Mesh(g, new THREE.MeshBasicMaterial({ color: rgb(c), transparent: true, opacity: c[3] * 0.45, depthWrite: false, side: THREE.DoubleSide })));
          var ds = ArNav.dots(poly, 0.15);
          if (ds.length) {
            var disc = new THREE.CircleGeometry(0.012, 8); disc.rotateX(-Math.PI / 2);
            var dots = new THREE.InstancedMesh(disc, new THREE.MeshBasicMaterial({ color: rgb(c), transparent: true, opacity: Math.min(1, c[3] * 2.2), depthWrite: false }), ds.length);
            var dm = new THREE.Matrix4();
            ds.forEach(function (d, i) { dm.makeTranslation(d[0], 0.006, d[1]); dots.setMatrixAt(i, dm); });
            dots.instanceMatrix.needsUpdate = true;
            p.node.add(dots);
          }
          var ep = []; poly.concat([poly[0]]).forEach(function (q) { ep.push(q[0], 0.006, q[1]); });
          var eg = new THREE.BufferGeometry(); attr(eg, 'position', ep, 3);
          p.node.add(new THREE.Line(eg, new THREE.LineBasicMaterial({ color: rgb(c) })));
        }
        var x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9; poly.forEach(function (q) { x0 = Math.min(x0, q[0]); x1 = Math.max(x1, q[0]); z0 = Math.min(z0, q[1]); z1 = Math.max(z1, q[1]); });
        p.size = { x: x1 - x0, y: z1 - z0 };
        if (isNew && st.planePrefab && window.DIORAMA_PREFABS && window.DIORAMA_PREFABS[st.planePrefabName]) {
          var made = spawn(st.planePrefabName); if (made) { made.node.parent && made.node.parent.remove(made.node); made.node.userData.prefab = true; p.node.add(made.node); }
        }
      }
      var cpos = new THREE.Vector3().setFromMatrixPosition(matrix);
      p.center = { x: cpos.x, y: cpos.y, z: cpos.z };
      (isNew ? PLANES.changed.added : PLANES.changed.updated).push(p);
    };
    PLANES.remove = function (id) {
      var p = PLANES.byId[id]; if (!p) return;
      delete PLANES.byId[id]; PLANES.list.splice(PLANES.list.indexOf(p), 1);
      if (p.node.parent) p.node.parent.remove(p.node);
      PLANES.changed.removed.push(p);
    };
    PLANES.flush = function () {   // scripts' onPlanesChanged({ added, updated, removed }), AR Foundation's planesChanged
      var c = PLANES.changed;
      if (!c.added.length && !c.updated.length && !c.removed.length) return;
      var info = function (p) { return { id: p.id, alignment: p.alignment, center: p.center, size: p.size, boundary: p.boundary }; };
      var ev = { added: c.added.map(info), updated: c.updated.map(info), removed: c.removed.map(info) };
      PLANES.changed = { added: [], updated: [], removed: [] };
      R.objects.forEach(function (g) { if (!g.dead) g.scripts.forEach(function (sc) { call(g, sc, 'onPlanesChanged', ev); }); });
    };
    // a ray from the screen (x, y: -1..1) onto the planes found: [{ position, distance, plane }], nearest first
    PLANES.raycast = function (x, y) {
      if (!R.camera || !PLANES.look) return [];
      var rc = new THREE.Raycaster(); rc.setFromCamera({ x: x, y: y }, R.camera);
      var meshes = []; PLANES.list.forEach(function (p) { p.node.updateMatrixWorld(true); p.node.children.forEach(function (c) { if (c.isMesh && !c.isInstancedMesh) { c.userData.plane = p; meshes.push(c); } }); });
      var hits = rc.intersectObjects(meshes, false).map(function (h) { return { position: { x: h.point.x, y: h.point.y, z: h.point.z }, distance: h.distance, plane: h.object.userData.plane.id }; });
      if (!hits.length && R.input.hit) hits.push({ position: R.input.hit, distance: 0, plane: 'hit-test' });   // a phone without plane detection: its hit test
      return hits;
    };
    R.input = { tapped: false, x: 0, y: 0 };
    R.cameraApi = { get position() { var p = new THREE.Vector3(); if (R.camera) R.camera.getWorldPosition(p); return { x: p.x, y: p.y, z: p.z }; } };
    var runtime = {
      begin: function (nodes, players) { R.nodes = nodes; R.players = players; },
      setCamera: function (camera, dom) {
        R.camera = camera; R.dom = dom;
        if (listener && !listener.parent) camera.add(listener);
        var ray = new THREE.Raycaster();
        var down = null;
        var tap = function (ev) {
          var r = dom.getBoundingClientRect(), pt = ev.changedTouches ? ev.changedTouches[0] : ev;
          if (!down || Math.hypot(pt.clientX - down.x, pt.clientY - down.y) > 8) return;   // a drag, not a tap
          var x = ((pt.clientX - r.left) / r.width) * 2 - 1, y = -((pt.clientY - r.top) / r.height) * 2 + 1;
          R.input.tapped = true; R.input.x = x; R.input.y = y;
          unlockMedia();
          if (camera.isPerspectiveCamera || camera.isOrthographicCamera) ray.setFromCamera({ x: x, y: y }, camera);
          else {   // ARToolKit's camera is a plain THREE.Camera with the webcam's own projection: the ray by hand
            camera.updateMatrixWorld();
            var inv = new THREE.Matrix4(); if (inv.invert) inv.copy(camera.projectionMatrix).invert(); else inv.getInverse(camera.projectionMatrix);
            var o = new THREE.Vector3().setFromMatrixPosition(camera.matrixWorld);
            var d = new THREE.Vector3(x, y, 0.5).applyMatrix4(inv).applyMatrix4(camera.matrixWorld).sub(o).normalize();
            ray.set(o, d);
          }
          var targets = []; Object.keys(R.roots).forEach(function (k) { if (R.roots[k].visible !== false) targets.push(R.roots[k]); });
          var hits = ray.intersectObjects(targets, true);
          for (var i = 0; i < hits.length; ++i) {
            var n = hits[i].object; while (n && !(n.userData && n.userData.go)) n = n.parent;
            if (n && n.userData.go && n.userData.go.scripts.some(function (s) { return s.fns.onTap; })) { var g = n.userData.go; g.scripts.forEach(function (s) { call(g, s, 'onTap'); }); break; }
          }
        };
        dom.addEventListener('pointerdown', function (ev) { down = { x: ev.clientX, y: ev.clientY }; });
        dom.addEventListener('pointerup', tap);
      },
      consumeTap: function () { R.input.tapped = false; R.input.hit = null; },   // a tap that did something else (locking)
      tapAt: function (x, y, hit) { R.input.tapped = true; R.input.x = x; R.input.y = y; R.input.hit = hit || null; unlockMedia(); },   // WebXR's select
      anchor: function (name, root, objects) { R.roots[name] = root; root.userData.objects = objects; },
      seen: function (name, on) {   // an anchor found or lost: its objects' onFound / onLost
        var root = R.roots[name]; if (!root) return;
        if (on) startMediaUnder(root);
        root.traverse(function (c) { var g = c.userData.go; if (g && !g.dead) g.scripts.forEach(function (s) { call(g, s, on ? 'onFound' : 'onLost'); }); });
      },
      update: function (dt, t) {
        R.t = t; R.dt = dt;
        navSample(); PLANES.flush();
        R.objects.forEach(function (g) {
          if (g.dead) return;
          g.scripts.forEach(function (s) { if (!s.started) { s.started = true; call(g, s, 'start'); } call(g, s, 'update', dt); });
          var v = g._vel; if (!g.data.hasBody && (v.x || v.y || v.z)) { var p = g.data.position; p[0] += v.x * dt; p[1] += v.y * dt; p[2] += v.z * dt; }
        });
        R.objects = R.objects.filter(function (g) { return !g.dead; });
        physicsStep(dt, t);
        R.input.tapped = false; R.input.hit = null;
      }
    };

    // an anchor's world: its lights, its window, its objects (nodes and animation players collected for the frame loop)
    function anchorWorld(m, nodes, players) {
      var root = new THREE.Group();
      var amb = new THREE.AmbientLight(rgb(m.ambient), THREE.SRGBColorSpace ? Math.PI : 1);
      var sun = new THREE.DirectionalLight(rgb(m.sun), m.sunStrength * (THREE.SRGBColorSpace ? Math.PI : 1));
      var sy = m.sunYaw * D2R, sp = m.sunPitch * D2R;
      var R = GFX.tracking === 'surfaces' ? 4 : 1.6;   // the shadows' reach: a marker's scene, or a room (metres)
      sun.position.set(Math.cos(sp) * Math.sin(sy) * R * 2.5, Math.sin(sp) * R * 2.5, Math.cos(sp) * Math.cos(sy) * R * 2.5);
      var sunAt = new THREE.Object3D(); root.add(sunAt); sun.target = sunAt;
      root.add(amb); root.add(sun);
      if (GFX.shadows && m.sunShadows !== false && GFX.tracking !== 'faces') {
        sun.castShadow = true; sun.shadow.mapSize.set(shadowMapSize(), shadowMapSize());
        var sc = sun.shadow.camera; sc.left = -R; sc.right = R; sc.top = R; sc.bottom = -R; sc.near = 0.05; sc.far = R * 6; sc.updateProjectionMatrix();
        sun.shadow.bias = -0.0005; sun.shadow.normalBias = 0.01;
        if (m.shadowCatcher !== false && !GFX.webgpu) {   // an invisible floor that only shows shadows: they fall on the real world (WebGL: three r172's WebGPU shadow material doesn't compile yet)
          var cg = new THREE.PlaneGeometry(R * 2.5, R * 2.5); cg.rotateX(-Math.PI / 2);
          var catcher = new THREE.Mesh(cg, new THREE.ShadowMaterial({ opacity: 0.35, depthWrite: false }));
          catcher.position.y = 0.001; catcher.receiveShadow = true; catcher.renderOrder = -0.5;
          root.add(catcher);
        }
      }
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
    // the Main Camera's objects: built under the camera itself, so they ride with the phone (find("Main Camera/Torch"))
    function cameraRig(proj, camera, nodes, players) {
      var cam = proj.camera; if (!cam || !cam.objects || !cam.objects.length) return null;
      var rig = new THREE.Group(), byId = {};
      cam.objects.forEach(function (o) {
        if (!o.active) return;
        var parent = o.parent === -1 ? rig : byId[o.parent];
        if (!parent) return;
        var n = build(o, players); byId[o.id] = n; parent.add(n); nodes.push(n); attachScripts(n);
      });
      camera.add(rig);
      runtime.begin(nodes, players);
      runtime.anchor('Main Camera', rig, cam.objects);
      return rig;
    }
    // the Main Camera's settings: the colour behind (when it's not the camera's picture), near, far, field of view
    function cameraSettings(proj, camera, opts) {
      var c = proj.camColour || [0.07, 0.07, 0.08];
      var colour = rgb(c);
      if (opts && opts.preview) { camera.fov = proj.camFov || 60; }
      if (opts && opts.ownProjection !== true) { camera.near = proj.camNear || 0.01; camera.far = proj.camFar || 100; if (camera.updateProjectionMatrix && camera.isPerspectiveCamera) camera.updateProjectionMatrix(); }
      return { solid: proj.camClear === 1, colour: colour };
    }
    return { anchorWorld: anchorWorld, animate: animate, runtime: runtime, planes: PLANES, nav: nav, navLoadSaved: navLoadSaved, cameraRig: cameraRig, cameraSettings: cameraSettings,
             graphics: graphics, setupRenderer: setupRenderer, makeComposer: makeComposer, environment: environment };
  }

  function fromDisk() {
    if (location.protocol !== 'file:') return false;
    // a page opened straight from the disk can't load its pictures and models: say how to play it
    say('Double-click Play.bat in this folder to play it here, or put the folder on a web host (https) for phones.');
    return true;
  }
  // the camera's calibration: this device's own (calibrate.html keeps it), else the project's (camera.json, from the
  // editor); null without one. { width, height, K: [fx, fy, cx, cy], dist, fovDeg: across }
  function calibration(proj) {
    var c = null;
    try { c = JSON.parse(localStorage.getItem('dio-camera') || 'null'); } catch (e) {}
    if (!c || !c.K) c = proj && proj.calibration;
    if (!c || !c.K || !c.width) return null;
    c.fovDeg = 2 * Math.atan(c.width / 2 / c.K[0]) * 180 / Math.PI;
    return c;
  }
  function withScene(cb) {
    var go = function (p) { window.DIORAMA_PROJECT = p; cb(p); };   // physics reads it (its gravity, anchors, floors)
    if (window.DIORAMA_SCENE) go(window.DIORAMA_SCENE); else loadJSON('scene.json', go);
  }

  // ---- Markers: jsartoolkit5 (three r103, loaded as plain scripts)
  // the splash screen (Player Settings): its picture, the game's name, the intro words, Tap to start. Once per visit.
  function splash() {
    var P = window.DIORAMA_PLAYER;
    if (!P || P.skip) return;
    try { if (sessionStorage.getItem('dio-splash')) return; } catch (e) {}
    var d = document.createElement('div');
    d.id = 'dio-splash';
    d.style.cssText = 'position:fixed;left:0;top:0;right:0;bottom:0;z-index:50;background:#111;color:#e6e6e6;display:flex;flex-direction:column;' +
      'align-items:center;justify-content:center;gap:16px;padding:24px;box-sizing:border-box;text-align:center;font-family:"Segoe UI",Roboto,Arial,sans-serif';
    var img = document.createElement('img'); img.src = P.splash; img.style.cssText = 'max-width:min(70vw,420px);max-height:45vh;object-fit:contain';
    var h = document.createElement('div'); h.textContent = P.title; h.style.cssText = 'font-size:28px;font-weight:600';
    var t = document.createElement('div'); t.textContent = P.intro || ''; t.style.cssText = 'font-size:16px;opacity:0.8;white-space:pre-wrap;max-width:560px';
    var b = document.createElement('button'); b.textContent = 'Tap to start';
    b.style.cssText = 'margin-top:8px;padding:12px 32px;font:inherit;font-size:18px;border:0;border-radius:6px;background:#007bb7;color:#fff;cursor:pointer';
    var c = document.createElement('div'); c.textContent = P.company ? P.company : ''; c.style.cssText = 'position:absolute;bottom:14px;font-size:12px;opacity:0.5';
    [img, h, t, b, c].forEach(function (e) { d.appendChild(e); });
    var go = function () {
      try { sessionStorage.setItem('dio-splash', '1'); } catch (e) {}
      d.parentNode && d.parentNode.removeChild(d);
      if (window.DioramaUnlock) window.DioramaUnlock();   // the first tap: sound and video may start now
    };
    d.addEventListener('click', go);
    document.body.appendChild(d);
  }
  function loadScripts(list, done) {   // in order
    if (!list.length) { done(); return; }
    var el = document.createElement('script'); el.src = list[0];
    el.onload = function () { loadScripts(list.slice(1), done); };
    el.onerror = function () { say('Couldn\'t load ' + list[0]); };
    document.head.appendChild(el);
  }
  function startMarkers(lib) {
    splash();
    if (fromDisk()) return;
    lib = lib || { THREE: window.THREE, GLTFLoader: window.THREE && window.THREE.GLTFLoader };
    var THREE = lib.THREE;
    // ARToolKit's three.js glue (artoolkit.three.js) was written for an older three: what it needs, under its old names
    if (!window.THREE || window.THREE !== THREE) {
      var shim = {}; for (var k in THREE) shim[k] = THREE[k];
      if (!shim.PlaneBufferGeometry) shim.PlaneBufferGeometry = THREE.PlaneGeometry;
      window.THREE = shim;
    }
    say('Loading the scene...');
    withScene(function (proj) {
      say('Starting the camera...');
      document.title = proj.title;
      var C = core(THREE, lib.GLTFLoader);
      C.graphics(proj);
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
            var renderer = makeRenderer(lib, { antialias: true });
            // the camera: ARToolKit's own projection (the webcam's lens); WebGPU's depth runs 0..1, WebGL's -1..1
            var arCam = arScene.camera, P = arCam.projectionMatrix.clone();
            arCam.near = 0.01; arCam.far = 1000;   // a plain THREE.Camera has none; WebGPU's shadow and depth nodes read them
            var toWebGPU = new THREE.Matrix4().set(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5, 0.5, 0, 0, 0, 1);
            arCam.updateProjectionMatrix = function () {
              this.projectionMatrix.copy(P);
              if (THREE.WebGPUCoordinateSystem !== undefined && this.coordinateSystem === THREE.WebGPUCoordinateSystem) this.projectionMatrix.premultiply(toWebGPU);
              this.projectionMatrixInverse.copy(this.projectionMatrix).invert();
            };
            arCam.updateProjectionMatrix();
            // the webcam picture is sRGB (today's three manages colour)
            // ARToolKit's own texture is made before the video has a size; today's three.js gives a texture fixed storage at
            // its first upload, so that one stays empty (a black background). A VideoTexture, made now the camera runs
            var webcamTex = new THREE.VideoTexture(arScene.video);
            if (THREE.SRGBColorSpace) webcamTex.colorSpace = THREE.SRGBColorSpace;
            arScene.videoScene.traverse(function (c) { if (c.material && c.material.map) { webcamTex.flipY = c.material.map.flipY; c.material.map = webcamTex; c.material.needsUpdate = true; } });
            var fit = function () {
              var vw = arController.videoWidth, vh = arController.videoHeight;
              if (arController.orientation === 'portrait') { var w = window.innerWidth / vh * vw, h = window.innerWidth; renderer.setSize(w, h); }
              else renderer.setSize(window.innerWidth, window.innerWidth / vw * vh);
            };
            var composer = null;
            fit(); window.addEventListener('resize', function () { fit(); if (composer) { var sz = renderer.getSize(new THREE.Vector2()); composer.setSize(sz.x, sz.y); } });
            C.setupRenderer(renderer);
            var camSet = C.cameraSettings(proj, arScene.camera, { ownProjection: true });   // ARToolKit's lens is the real one
            if (camSet.solid) { arScene.videoScene.traverse(function (c) { if (c.isMesh) c.visible = false; }); if (renderer.setClearColor) renderer.setClearColor(camSet.colour, 1); }
            document.body.insertBefore(renderer.domElement, document.body.firstChild);
            C.runtime.setCamera(arScene.camera, renderer.domElement);
            var nodes = [], players = [], roots = [];
            C.environment(arScene.scene, proj.markers[0] && proj.markers[0].ambient);
            C.cameraRig(proj, arScene.camera, nodes, players);
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
              if (composer) composer.render(); else arScene.renderOn(renderer);
            };
            var started = renderer.init ? renderer.init() : Promise.resolve();
            Promise.resolve(started).then(function () {
              if (usesWebGPU(renderer)) {   // WebGPU reads video textures the other way up from WebGL
                console.log('Diorama: drawing with WebGPU');
                arScene.videoScene.traverse(function (c) { if (c.isMesh) c.scale.y *= -1; });
              }
              arCam.coordinateSystem = renderer.coordinateSystem !== undefined ? renderer.coordinateSystem : arCam.coordinateSystem;
              arCam.updateProjectionMatrix();
              renderer.setAnimationLoop(tick);
              if (arController.orientation !== 'portrait' && !camSet.solid) C.makeComposer(renderer, arScene.scene, arScene.camera, true).then(function (c) {
                if (!c) return;
                var bg = new THREE.VideoTexture(arScene.video); if (THREE.SRGBColorSpace) bg.colorSpace = THREE.SRGBColorSpace;
                arScene.scene.background = bg;   // the camera's picture behind, through the composer
                composer = c;
              });
            });
          }
        });
        delete window.ARThreeOnLoad;
      };
      if (window.ARController && ARController.getUserMediaThreeScene) window.ARThreeOnLoad();
      else if (!window.artoolkit) loadScripts(['lib/artoolkit.min.js', 'lib/artoolkit.three.js'], function () {});
    });
  }

  // ---- Pictures and faces: MindAR (three 0.160, ES modules: the page hands in what it imported)
  function startMind(lib) {
    splash();
    if (fromDisk()) return;
    var THREE = lib.THREE, faces = lib.mode === 'face';
    say('Loading the scene...');
    withScene(function (proj) {
      document.title = proj.title;
      var C = core(THREE, lib.GLTFLoader);
      C.graphics(proj);
      var anchors = proj.markers;
      var go = function (imageTargetSrc) {
        say('Starting the camera...');
        var opts = { container: document.body, uiLoading: 'no', uiScanning: 'no', uiError: 'no' };
        if (!faces) opts.imageTargetSrc = imageTargetSrc;
        var mind = new lib.MindARThree(opts);
        var renderer = mind.renderer, scene = mind.scene, camera = mind.camera;
        C.setupRenderer(renderer);
        C.environment(scene, anchors[0] && anchors[0].ambient);
        if (!camera.parent) scene.add(camera);
        var camSet = C.cameraSettings(proj, camera, { ownProjection: true });
        if (camSet.solid) {   // the webcam picture hidden: a world of its own
          document.documentElement.style.background = '#' + camSet.colour.getHexString();
          var hideVideo = setInterval(function () { var v = document.querySelector('video'); if (v) { v.style.visibility = 'hidden'; clearInterval(hideVideo); } }, 200);
        }
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
        C.cameraRig(proj, camera, nodes, players);
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


  // ---------- Surfaces: plane detection (WebXR: the phone's own AR, ARCore), a reticle, tap to place; a preview elsewhere ----------
  function startSurfaces(lib) {
    splash();
    if (fromDisk()) return;
    var THREE = lib.THREE;
    say('Loading the scene...');
    withScene(function (proj) {
      document.title = proj.title;
      var C = core(THREE, lib.GLTFLoader);
      C.graphics(proj);
      var renderer = null;
      var useRenderer = function (gpu) {   // WebXR needs WebGL; the preview can draw with WebGPU
        renderer = makeRenderer({ THREE: THREE, webgpu: gpu }, { antialias: true, alpha: true });
        renderer.setPixelRatio(window.devicePixelRatio || 1);
        renderer.setSize(window.innerWidth, window.innerHeight);
        renderer.domElement.style.cssText = 'position:fixed;left:0;top:0;width:100vw;height:100vh';
        document.body.insertBefore(renderer.domElement, document.body.firstChild);
        C.runtime.setCamera(camera, renderer.domElement);
        C.setupRenderer(renderer);
      };
      var composer = null;
      var scene = new THREE.Scene(), camera = new THREE.PerspectiveCamera(70, window.innerWidth / window.innerHeight, 0.01, 200);
      scene.add(camera);
      var first = proj.markers[0] || {};
      C.planes.settings = { planeDetect: first.planeDetect === undefined ? 2 : first.planeDetect, showPlanes: first.showPlanes !== false,
        planeColour: first.planeColour || [0.25, 0.95, 0.55, 0.35], planePrefab: first.planePrefab,
        planePrefabName: first.planePrefab ? String(first.planePrefab).split('/').pop().replace(/\.prefab$/, '') : '' };
      C.planes.look = new THREE.Group(); scene.add(C.planes.look);
      C.environment(scene, first.ambient);
      var nodes = [], players = [];
      var anchors = proj.markers.map(function (m) { var root = C.anchorWorld(m, nodes, players); root.visible = false; scene.add(root); return { m: m, root: root, placed: false }; });
      C.cameraRig(proj, camera, nodes, players);
      var camSet = C.cameraSettings(proj, camera, { preview: true });
      C.navLoadSaved();
      window.addEventListener('resize', function () { if (renderer) renderer.setSize(window.innerWidth, window.innerHeight); if (composer) composer.setSize(window.innerWidth, window.innerHeight); camera.aspect = window.innerWidth / window.innerHeight; camera.updateProjectionMatrix(); });
      // where an anchor goes: a point on a surface, turned to face the player
      var place = function (a, pos) {
        var cam = new THREE.Vector3(); camera.getWorldPosition(cam);
        a.root.position.copy(pos);
        a.root.rotation.set(0, Math.atan2(cam.x - pos.x, cam.z - pos.z), 0);
        if (!a.placed) { a.placed = true; a.root.visible = true; C.runtime.seen(a.m.name, true); }
      };
      var clock = new THREE.Clock(), t = 0;
      var tick = function (frameCb) {
        return function (time, frame) {
          var dt = Math.min(0.1, clock.getDelta()); t += dt;
          if (frameCb) frameCb(frame);
          C.runtime.update(dt, t);
          nodes.forEach(function (n) { if (!n.userData.go || !n.userData.go.dead) C.animate(n, t); });
          players.forEach(function (p) { p.update(dt); });
          if (composer && !(renderer.xr && renderer.xr.isPresenting)) composer.render(); else renderer.render(scene, camera);
        };
      };

      // the preview (a PC, or a phone without ARCore): the editor's pretend room, its planes "found" one by one
      var preview = function (why) {
        say(why);
        useRenderer(!!lib.webgpu);
        C.makeComposer(renderer, scene, camera, false).then(function (c) { composer = c; });
        scene.background = camSet.colour;
        var yaw = 0.35, pitch = 0.55, dist = 6.5, tgt = new THREE.Vector3(0.2, 0.4, -0.6);
        var aim = function () { camera.position.set(tgt.x + dist * Math.cos(pitch) * Math.sin(yaw), tgt.y + dist * Math.sin(pitch), tgt.z + dist * Math.cos(pitch) * Math.cos(yaw)); camera.lookAt(tgt); };
        aim();
        var drag = null;
        renderer.domElement.addEventListener('pointerdown', function (e) { drag = { x: e.clientX, y: e.clientY }; });
        window.addEventListener('pointerup', function () { drag = null; });
        window.addEventListener('pointermove', function (e) { if (!drag) return; yaw -= (e.clientX - drag.x) * 0.006; pitch = Math.max(0.05, Math.min(1.5, pitch + (e.clientY - drag.y) * 0.006)); drag = { x: e.clientX, y: e.clientY }; aim(); });
        renderer.domElement.addEventListener('wheel', function (e) { dist = Math.max(1, Math.min(30, dist * (e.deltaY > 0 ? 1.1 : 0.9))); aim(); }, { passive: true });
        var found = 0;
        var findNext = function () {
          if (found >= ArNav.simPlanes.length) return;
          var sp = ArNav.simPlanes[found++];
          var m = new THREE.Matrix4().makeTranslation(sp.position[0], sp.position[1], sp.position[2]).multiply(new THREE.Matrix4().makeRotationX(sp.rotX));
          C.planes.set(sp.id, m, sp.poly, sp.alignment);
          setTimeout(findNext, 700);
        };
        setTimeout(findNext, 400);
        // placement: on the floor (tap places where the floor is hit)
        anchors.forEach(function (a) { if (a.m.placement !== 0) place(a, new THREE.Vector3(0, 0, 0)); });
        renderer.domElement.addEventListener('click', function (e) {
          var r = renderer.domElement.getBoundingClientRect();
          var hits = C.planes.raycast(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
          anchors.forEach(function (a) { if (a.m.placement === 0 && hits.length) place(a, new THREE.Vector3(hits[0].position.x, hits[0].position.y, hits[0].position.z)); });
        });
        renderer.setAnimationLoop(tick(null));
      };


      // any camera (a PC's webcam, a phone without ARCore): the camera's picture, a tap locks the table (surfacetrack.js,
      // OpenCV.js), then it's followed frame by frame. Its tilt: the phone's gravity, else the AR Plane Manager's setting
      var webcam = function (why) {
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) { preview(why); return; }
        say('Starting the camera...');
        navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment', width: { ideal: 1280 }, height: { ideal: 720 } }, audio: false }).then(function (stream) {
          var video = document.createElement('video');
          video.playsInline = true; video.muted = true; video.srcObject = stream; video.setAttribute('playsinline', '');
          video.style.cssText = 'position:fixed;left:0;top:0;width:100vw;height:100vh;object-fit:cover;z-index:-1';
          document.body.insertBefore(video, document.body.firstChild);
          document.documentElement.style.background = '#000';
          video.play();
          useRenderer(!!lib.webgpu);
          if (renderer.setClearColor) renderer.setClearColor(0x000000, 0);
          var calib = calibration(proj);
          var settings = { fovDeg: calib ? calib.fovDeg : first.webcamFov || 60, tiltDeg: first.webcamTilt || 50, height: first.webcamHeight || 0.6, facing: first.webcamSurface === 2 };
          var gravityTilt = null;   // the phone's own: how far it looks down
          window.addEventListener('deviceorientation', function (e) { if (e.beta !== null && e.beta !== undefined) gravityTilt = Math.max(15, Math.min(89, 90 - Math.abs(e.beta))); });
          var surface = new THREE.Group(); surface.matrixAutoUpdate = false; surface.visible = false; scene.add(surface);
          anchors.forEach(function (a) { scene.remove(a.root); surface.add(a.root); });
          var fit = function () {   // the camera's view, as the picture fills the screen (object-fit: cover)
            var vw = video.videoWidth || 1280, vh = video.videoHeight || 720, sw = window.innerWidth, sh = window.innerHeight;
            var hfov = settings.fovDeg * Math.PI / 180, va = vw / vh, scale = Math.max(sw / vw, sh / vh);
            var vfov = 2 * Math.atan(Math.tan(hfov / 2) / va);            // the video's own, up and down
            camera.fov = 2 * Math.atan(Math.tan(vfov / 2) * (sh / (vh * scale))) * 180 / Math.PI;
            camera.aspect = sw / sh; camera.updateProjectionMatrix();
          };
          video.addEventListener('loadedmetadata', fit); window.addEventListener('resize', fit);
          var tracker = null, lostAt = 0, F = new THREE.Matrix4().makeScale(1, -1, -1), M = new THREE.Matrix4();
          var poly = [[-1.5, -1.5], [1.5, -1.5], [1.5, 1.5], [-1.5, 1.5]];
          var relock = document.createElement('button'); relock.textContent = 'Lock';
          relock.style.cssText = 'position:fixed;left:50%;bottom:40px;transform:translateX(-50%);z-index:40;padding:12px 34px;font:inherit;font-size:17px;border:0;border-radius:6px;background:#007bb7;color:#fff;display:none';
          document.body.appendChild(relock);
          // before locking: what it can see (the features) as dots, and the middle of the view
          var overlay = document.createElement('canvas');
          overlay.style.cssText = 'position:fixed;left:0;top:0;width:100vw;height:100vh;z-index:30;pointer-events:none';
          document.body.appendChild(overlay);
          var octx = overlay.getContext('2d'), lastPreview = 0;
          var drawPreview = function (pv) {
            var sw = window.innerWidth, sh = window.innerHeight;
            if (overlay.width !== sw || overlay.height !== sh) { overlay.width = sw; overlay.height = sh; }
            octx.clearRect(0, 0, sw, sh);
            if (!pv) return;
            var vw = video.videoWidth || 1280, vh = video.videoHeight || 720, sc = Math.max(sw / vw, sh / vh), ox = (sw - vw * sc) / 2, oy = (sh - vh * sc) / 2;
            octx.fillStyle = 'rgba(90, 240, 140, 0.9)';
            for (var i = 0; i < pv.points.length; i += 2) octx.fillRect(ox + pv.points[i] * sc - 2, oy + pv.points[i + 1] * sc - 2, 4, 4);
            var cx = sw / 2, cy = sh / 2;   // the part that's locked: the middle 70% of the camera's picture
            octx.strokeStyle = 'rgba(255,255,255,0.8)'; octx.lineWidth = 2;
            octx.strokeRect(Math.max(4, ox + vw * sc * 0.15), Math.max(4, oy + vh * sc * 0.15), Math.min(sw - 8, vw * sc * 0.7), Math.min(sh - 8, vh * sc * 0.7));
            octx.beginPath(); octx.moveTo(cx - 12, cy); octx.lineTo(cx + 12, cy); octx.moveTo(cx, cy - 12); octx.lineTo(cx, cy + 12); octx.stroke();
          };
          var lock = function () {
            if (!tracker) return;
            if (gravityTilt !== null) tracker.s.tiltDeg = gravityTilt;
            // auto: a phone held upright (looking ahead) locks onto what faces it
            tracker.s.facing = first.webcamSurface === 2 || (first.webcamSurface !== 1 && gravityTilt !== null && gravityTilt < 28);
            C.runtime.consumeTap();   // the tap that locks only locks
            if (!tracker.lock(video)) { say('Not enough to see there: point at a table or floor with some pattern, in good light, then tap.'); return; }
            var P = window.SurfaceTrack.lockPose(tracker.s);
            show(P);
            anchors.forEach(function (a) { if (!a.placed) { a.placed = true; a.root.visible = true; C.runtime.seen(a.m.name, true); } });
            relock.textContent = 'Re-lock'; relock.style.left = '12px'; relock.style.bottom = '12px'; relock.style.transform = 'none';
            relock.style.padding = '8px 16px'; relock.style.fontSize = '';
            drawPreview(null);
            say('Locked. Move slowly; Re-lock to choose another spot.');
          };
          var show = function (P) {   // the surface, where the camera sees it (OpenCV's camera turned into three's)
            var R = P.R, t = P.t;
            M.set(R[0], R[1], R[2], t[0], R[3], R[4], R[5], t[1], R[6], R[7], R[8], t[2], 0, 0, 0, 1).premultiply(F);
            camera.updateMatrixWorld();
            surface.matrix.copy(camera.matrixWorld).multiply(M); surface.matrixWorldNeedsUpdate = true; surface.visible = true;
            if (++outlineTick % 5 === 1) { var b = tracker.boundary(); if (b.length >= 3) poly = b; }   // its outline, as it grows (the map's hull)
            C.planes.set('surface', surface.matrix, poly, 'horizontal');
          };
          // walls (V): each its own tracker, facing the camera; drawn and hit like the first plane
          var walls = [], outlineTick = 0, goodSince = 0;
          var lockWall = function () {
            if (!tracker || !window.SurfaceTrack) return;
            var w = new window.SurfaceTrack.Tracker(tracker.cv, Object.assign({}, settings, { facing: true }));
            if (!w.lock(video)) { say('Not enough to see on that wall: point at something with a pattern.'); return; }
            walls.push({ tr: w, id: 'wall ' + (walls.length + 1), poly: poly.slice(), node: new THREE.Object3D(), lostAt: 0 });
            say('A wall: ' + walls.length + ' found. V for another.');
          };
          window.addEventListener('keydown', function (e) { if (e.key === 'v' || e.key === 'V') lockWall(); if (e.key === ' ' && tracker && !tracker.locked()) lock(); });
          relock.addEventListener('click', function (e) { e.stopPropagation(); lock(); });
          renderer.domElement.addEventListener('click', function (e) {
            if (!tracker) return;
            if (!tracker.locked()) { lock(); return; }
            // later taps: tap to place, on the surface
            var r = renderer.domElement.getBoundingClientRect();
            var hits = C.planes.raycast(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
            if (hits.length) anchors.forEach(function (a) {
              if (a.m.placement !== 0) return;
              var v = new THREE.Vector3(hits[0].position.x, hits[0].position.y, hits[0].position.z); surface.worldToLocal(v);
              a.root.position.set(v.x, 0, v.z);
            });
          });
          // OpenCV.js: only here (it's big), then the tracker
          say('Loading the surface finder...');
          loadScripts(['lib/opencv/opencv.js', 'lib/surfacetrack.js'], function () {
            var ready = function (cv) {
              tracker = new window.SurfaceTrack.Tracker(cv, settings);
              relock.style.display = '';
              say('Point the camera at a table, the floor or a wall with some pattern (the green dots), then Lock.');
            };
            var wait = setInterval(function () {
              if (typeof window.cv === 'undefined') return;
              if (window.cv instanceof Promise) { clearInterval(wait); window.cv.then(ready); }
              else if (window.cv.Mat) { clearInterval(wait); ready(window.cv); }
            }, 100);
          });
          renderer.setAnimationLoop(tick(function () {
            if (tracker && !tracker.locked() && video.readyState >= 2 && t - lastPreview > 0.15) {   // before locking: what it sees
              lastPreview = t;
              var pv = tracker.preview(video);
              drawPreview(pv);
              relock.disabled = pv.count < 40;
              relock.style.opacity = pv.count < 40 ? 0.5 : 1;
              say(pv.count < 40 ? 'Too plain or dark here (' + pv.count + ' features): point at something with a pattern, in good light.'
                                : pv.count + ' features: looking for a plane (or Lock / tap / Space now; V for a wall).');
              // the first plane finds itself (as ARCore's): the middle detailed enough for a second
              if (tracker.lockable(video)) { if (!goodSince) goodSince = t; else if (t - goodSince > 1) { goodSince = 0; lock(); } } else goodSince = 0;
            }
            walls.forEach(function (w) {   // the walls: tracked, outlined, kept for raycasts
              var r = w.tr.track(video);
              if (r.ok) {
                w.lostAt = 0;
                M.set(r.R[0], r.R[1], r.R[2], r.t[0], r.R[3], r.R[4], r.R[5], r.t[1], r.R[6], r.R[7], r.R[8], r.t[2], 0, 0, 0, 1).premultiply(F);
                camera.updateMatrixWorld();
                var mw = new THREE.Matrix4().copy(camera.matrixWorld).multiply(M);
                if (++outlineTick % 5 === 1) { var b = w.tr.boundary(); if (b.length >= 3) w.poly = b; }
                C.planes.set(w.id, mw, w.poly, 'vertical');
              } else if (!w.lostAt) w.lostAt = t;
            });
            if (!tracker || !tracker.locked() || video.readyState < 2) return;
            var r = tracker.track(video);
            if (r.ok) { lostAt = 0; show({ R: r.R, t: r.t }); }
            else {
              if (!lostAt) lostAt = t;
              if (t - lostAt > 0.6) { surface.visible = false; say('Lost the surface: point back at it (or Re-lock).'); }
            }
          }));
        }).catch(function (e) { preview('No camera (' + (e && (e.message || e.name) || e) + '). ' + why); });
      };

      // the real thing: an immersive-ar session with hit-test and (when the phone has it) plane detection
      var startXR = function () {
        if (!renderer) useRenderer(false);
        var opts = { requiredFeatures: ['hit-test', 'local'], optionalFeatures: ['plane-detection', 'dom-overlay', 'anchors', 'light-estimation'], domOverlay: { root: document.body } };
        navigator.xr.requestSession('immersive-ar', opts).then(function (session) {
          renderer.xr.enabled = true;
          renderer.xr.setReferenceSpaceType('local');
          return renderer.xr.setSession(session).then(function () {
            say('Move the phone slowly over the floor.');
            var ref = renderer.xr.getReferenceSpace(), hitSource = null, seen = new Map(), nextId = 1;
            session.requestReferenceSpace('viewer').then(function (vs) { return session.requestHitTestSource({ space: vs }); }).then(function (h) { hitSource = h; }).catch(function () {});
            var ring = new THREE.Mesh(new THREE.RingGeometry(0.07, 0.09, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0xffffff }));
            ring.matrixAutoUpdate = false; ring.visible = false; scene.add(ring);
            var floorY = null;
            session.addEventListener('select', function () {
              if (!ring.visible) return;
              var pos = new THREE.Vector3().setFromMatrixPosition(ring.matrix);
              C.runtime.tapAt(0, 0, { x: pos.x, y: pos.y, z: pos.z });   // scripts: a tap at the middle (the reticle)
              anchors.forEach(function (a) { if (a.m.placement === 0) place(a, pos); });
            });
            session.addEventListener('end', function () { say('AR ended'); });
            renderer.setAnimationLoop(tick(function (frame) {
              if (!frame) return;
              // the reticle: where a tap would place things
              if (hitSource) {
                var hs = frame.getHitTestResults(hitSource);
                if (hs.length) { var hp = hs[0].getPose(ref); ring.visible = true; ring.matrix.fromArray(hp.transform.matrix); } else ring.visible = false;
              }
              // the planes the phone has found
              if (frame.detectedPlanes) {
                var now = new Set();
                frame.detectedPlanes.forEach(function (pl) {
                  now.add(pl);
                  var rec = seen.get(pl);
                  if (rec && rec.changed === pl.lastChangedTime) return;
                  var pose = frame.getPose(pl.planeSpace, ref); if (!pose) return;
                  if (!rec) { rec = { id: 'plane-' + (nextId++) }; seen.set(pl, rec); }
                  rec.changed = pl.lastChangedTime;
                  var mtx = new THREE.Matrix4().fromArray(pose.transform.matrix);
                  C.planes.set(rec.id, mtx, pl.polygon.map(function (q) { return [q.x, q.z]; }), pl.orientation === 'vertical' ? 'vertical' : 'horizontal');
                  if (pl.orientation !== 'vertical') { var y = pose.transform.position.y, cy = camera.position.y; if (y < cy - 0.5 && (floorY === null || y < floorY)) floorY = y; }
                });
                seen.forEach(function (rec, pl) { if (!now.has(pl)) { C.planes.remove(rec.id); seen.delete(pl); } });
              }
              // placement 1: on the first floor found, ahead of the player; 2: round where the phone started
              anchors.forEach(function (a) {
                if (a.placed) return;
                if (a.m.placement === 1 && floorY !== null) {
                  var cam = new THREE.Vector3(), fwd = new THREE.Vector3(); camera.getWorldPosition(cam); camera.getWorldDirection(fwd); fwd.y = 0; fwd.normalize();
                  place(a, new THREE.Vector3(cam.x + fwd.x * 1.2, floorY, cam.z + fwd.z * 1.2));
                } else if (a.m.placement === 2) place(a, new THREE.Vector3(0, floorY !== null ? floorY : -1.4, 0));
                else if (a.m.placement === 0 && ring.visible) say('Tap to place');
              });
            }));
          });
        }).catch(function (e) { preview('AR didn\'t start (' + (e && (e.message || e.name) || e) + '): a preview instead. Drag to look round.'); });
      };
      if (navigator.xr && navigator.xr.isSessionSupported) {
        navigator.xr.isSessionSupported('immersive-ar').then(function (ok) {
          if (!ok) { webcam('A preview instead: drag to look round, tap the floor to place.'); return; }
          // the browser lets AR start from a tap only
          var b = document.createElement('button'); b.textContent = 'Start AR';
          b.style.cssText = 'position:fixed;left:50%;bottom:64px;transform:translateX(-50%);z-index:40;padding:14px 36px;font:inherit;font-size:18px;border:0;border-radius:6px;background:#007bb7;color:#fff';
          useRenderer(false);
          b.addEventListener('click', function () { b.remove(); startXR(); });
          document.body.appendChild(b);
          say('Tap Start AR, then move the phone slowly over the floor.');
        }).catch(function () { webcam('A preview instead: drag to look round.'); });
      } else webcam('A preview instead: drag to look round, tap the floor to place.');
    });
  }


  // ---------- ArUco markers (OpenCV's square markers), with any camera: each number its own anchor ----------
  function startAruco(lib) {
    splash();
    if (fromDisk()) return;
    var THREE = lib.THREE;
    say('Loading the scene...');
    withScene(function (proj) {
      document.title = proj.title;
      var C = core(THREE, lib.GLTFLoader);
      C.graphics(proj);
      var renderer = makeRenderer(lib, { antialias: true });
      renderer.setPixelRatio(window.devicePixelRatio || 1);
      renderer.setSize(window.innerWidth, window.innerHeight);
      renderer.domElement.style.cssText = 'position:fixed;left:0;top:0;width:100vw;height:100vh';
      document.body.insertBefore(renderer.domElement, document.body.firstChild);
      C.setupRenderer(renderer);
      var scene = new THREE.Scene(), camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.01, 200);
      scene.add(camera);
      C.runtime.setCamera(camera, renderer.domElement);
      C.environment(scene, proj.markers[0] && proj.markers[0].ambient);
      var nodes = [], players = [];
      // each anchor on its marker: a holder the tracker moves, the anchor's world in it
      var anchors = proj.markers.map(function (m) {
        var holder = new THREE.Group(); holder.matrixAutoUpdate = false; holder.visible = false;
        holder.add(C.anchorWorld(m, nodes, players)); scene.add(holder);
        return { m: m, holder: holder, seen: -1, was: false };
      });
      C.cameraRig(proj, camera, nodes, players);
      var camSet = C.cameraSettings(proj, camera, { ownProjection: true });
      var calib = calibration(proj);
      var fovDeg = calib ? calib.fovDeg : (proj.markers[0] && proj.markers[0].webcamFov) || 60;
      var composer = null;
      say('Starting the camera...');
      navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment', width: { ideal: 1280 }, height: { ideal: 720 } }, audio: false }).then(function (stream) {
        var video = document.createElement('video');
        video.playsInline = true; video.muted = true; video.srcObject = stream; video.setAttribute('playsinline', '');
        video.play();
        var bg = new THREE.VideoTexture(video); if (THREE.SRGBColorSpace) bg.colorSpace = THREE.SRGBColorSpace;
        if (!camSet.solid) scene.background = bg; else scene.background = camSet.colour;
        // the camera's picture fills the screen (cropped, like object-fit: cover); the 3D camera sees just that part
        var fit = function () {
          var vw = video.videoWidth || 1280, vh = video.videoHeight || 720, sw = window.innerWidth, sh = window.innerHeight;
          renderer.setSize(sw, sh); if (composer) composer.setSize(sw, sh);
          var va = vw / vh, sa = sw / sh;
          if (sa > va) { bg.repeat.set(1, va / sa); bg.offset.set(0, (1 - va / sa) / 2); } else { bg.repeat.set(sa / va, 1); bg.offset.set((1 - sa / va) / 2, 0); }
          var vfov = 2 * Math.atan(Math.tan(fovDeg * Math.PI / 360) / va);                 // the video's own, up and down
          var shown = sa > va ? va / sa : 1;                                                    // how much of its height shows
          camera.fov = 2 * Math.atan(Math.tan(vfov / 2) * shown) * 180 / Math.PI;
          camera.aspect = sa; camera.updateProjectionMatrix();
        };
        video.addEventListener('loadedmetadata', fit); window.addEventListener('resize', fit);
        C.makeComposer(renderer, scene, camera, true).then(function (c) { composer = c; if (c) fit(); });
        var tracker = null;
        say('Loading the marker finder...');
        loadScripts(['lib/opencv/opencv.js', 'lib/arucotrack.js'], function () {
          var ready = function (cv) {
            tracker = new window.ArucoTrack.Tracker(cv, { dict: lib.dict || 0, fovDeg: fovDeg, K: calib && calib.K, dist: calib && calib.dist, calibWidth: calib && calib.width });
            say('Point the camera at an ArUco marker.');
          };
          var wait = setInterval(function () {
            if (typeof window.cv === 'undefined') return;
            if (window.cv instanceof Promise) { clearInterval(wait); window.cv.then(ready); }
            else if (window.cv.Mat) { clearInterval(wait); ready(window.cv); }
          }, 100);
        });
        var F = new THREE.Matrix4().makeScale(1, -1, -1), M = new THREE.Matrix4().set(1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1), P = new THREE.Matrix4();
        var clock = new THREE.Clock(), t = 0, lastLine = '';
        renderer.setAnimationLoop(function () {
          var dt = Math.min(0.1, clock.getDelta()); t += dt;
          if (tracker && video.readyState >= 2) {
            var found = tracker.detect(video);
            camera.updateMatrixWorld();
            anchors.forEach(function (a) {
              var f = null; found.forEach(function (q) { if (q.id === (a.m.arucoId || 0)) f = q; });
              if (f) {   // OpenCV's camera turned into three's; the marker's axes into the anchor's (Y up out of it, its top -Z)
                var R = f.R, tv = f.t;
                P.set(R[0], R[1], R[2], tv[0], R[3], R[4], R[5], tv[1], R[6], R[7], R[8], tv[2], 0, 0, 0, 1);
                a.holder.matrix.copy(camera.matrixWorld).multiply(F).multiply(P).multiply(M);
                a.holder.matrixWorldNeedsUpdate = true; a.holder.visible = true; a.seen = t;
              } else if (t - a.seen > 0.35) a.holder.visible = false;   // tracking flickers: a moment where it last was
              if (a.was !== a.holder.visible) { a.was = a.holder.visible; C.runtime.seen(a.m.name, a.was); }
            });
            var names = anchors.filter(function (a) { return a.holder.visible; }).map(function (a) { return a.m.name; });
            var line = names.length ? 'Found: ' + names.join(', ') : 'Point the camera at an ArUco marker.';
            if (line !== lastLine) { say(line); lastLine = line; }
          }
          C.runtime.update(dt, t);
          nodes.forEach(function (n) { if (!n.userData.go || !n.userData.go.dead) C.animate(n, t); });
          players.forEach(function (pl) { pl.update(dt); });
          if (composer) composer.render(); else renderer.render(scene, camera);
        });
      }).catch(function (e) { say('The camera didn\'t start: ' + (e && (e.message || e.name) || e)); });
    });
  }

  window.Diorama = { say: say, start: startMarkers, startMarkers: startMarkers, startMind: startMind, startSurfaces: startSurfaces, startAruco: startAruco, ArNav: ArNav };
})();
