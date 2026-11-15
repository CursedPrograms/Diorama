[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

<br>
<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" style="width: 20%;"/>
  </a>
</div>
<br>

<div align="center">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-%23323330.svg?&style=for-the-badge&logo=c%2B%2B&logoColor=white"/>
  <img alt="JavaScript" src="https://img.shields.io/badge/JavaScript-%23323330.svg?&style=for-the-badge&logo=javascript&logoColor=white"/>
</div>
<br>

<div align="center">
  <img src="assets/logo.png" alt="Diorama" width="160">
</div>

# Diorama

**Little worlds in the real one.** Diorama is Atelier's augmented reality engine, laid out like Unity: build scenes on
printed markers, pictures, faces or real tables and floors, script them in JavaScript, and build them into a website,
a Windows program, a Linux or Raspberry Pi game, or an Android app. No Unity, no app store, no webcam needed to make
the markers. Part of [Atelier](https://github.com/CursedPrograms/Atelier).

**Website:** https://cursedprograms.github.io/Diorama/

![Diorama: the art gallery scene, a frame on a marker with a forest down inside it](images/window.jpg)

## Contents

- [What it does](#what-it-does)
- [Tutorials: make the templates yourself](#tutorials-make-the-templates-yourself)
  - [1. A cube on Hiro](#1-a-cube-on-hiro)
  - [2. The art gallery](#2-the-art-gallery)
  - [3. Plane detection: tap to place cubes](#3-plane-detection-tap-to-place-cubes)
  - [4. Indoor navigation](#4-indoor-navigation)
- [Scripting reference](#scripting-reference)
- [Install](#install) · [Build](#build) · [Credits](#credits)

## What it does

- **Laid out like Unity:** a Hierarchy (the **Main Camera**, then anchors with their objects; drag to parent), the
  Scene view, an Inspector of components (each with its tick, Reset and Remove, **Add Component**), a Project window
  for every asset, a Console, a Script Editor. **Q W E R** handles, **F** to frame, Ctrl+D, Delete, Ctrl+Z / Ctrl+Y;
  Transform values drag and the view follows live.
- **What it tracks** (Edit > Project Settings), in plain words:
  - **Markers:** Hiro, Kanji, your own picture or letters (the `.patt` is made from the picture: no webcam), 3x3
    barcodes 0..63, and the **Marker Maker** (Window menu: random barcodes, random block patterns, print sheets).
  - **Images:** any photo, poster or page (MindAR).
  - **Faces:** things on the player's face (MindAR face).
  - **Surfaces:** floors, tables and walls. **Plane detection** on ARCore phones (WebXR), and on **any camera**
    (PCs, phones without ARCore): point at a table or wall, Lock, and it's followed (CLAHE, ORB, RANSAC, solvePnP;
    OpenCV.js in the game, OpenCV in C++). AR Foundation's workflow: AR Plane Manager, `planesChanged`, raycasts.
- **Objects:** empties, primitives, see-through pictures, **glTF/GLB models with bones and animation clips**, sprite
  flocks, light rays, words.
- **Components:** Light (point, spot, directional), Audio Source (3D), **Video Player** (with alpha: WebM, MOV ProRes
  4444 / PNG, GIF; or a green screen), Particle System, Line Renderer, Trail Renderer, Occluder, Animate, **AR Path**
  (route marking with arrows) and **AR Path Follower** (a guide that walks it), **Canvas UI** (Text, Button, Slider,
  Toggle, Image, Panel as real HTML/CSS), **Scripts**.
- **Scenes** (Window > Scenes): scene 0 is the game's `index.html`; `loadScene()` moves between them.
- **Build Settings** (Ctrl+Shift+B): **Web** (a static website), **Windows** (`<Game>.exe`), **Linux**, **Raspberry Pi**
  (kiosk, starts with the Pi) and **Android** (`.apk`, `com.cursedentertainment.<game>`); a splash screen with intro
  words; WebGL 2 or **WebGPU**; builds run in the background with Cancel; **Build And Run** (Ctrl+B).
- **▶ Play** (Ctrl+P) builds and opens the game in your browser with the webcam; `log()` and errors come back to the
  Console, on their lines in the Script Editor.

## Tutorials: make the templates yourself

Each template is in File > New from template; these steps build the same thing from an empty project, so you know
where every part comes from. A new project gets its own folder (Documents\Diorama, or `%USERPROFILE%\Diorama` when
Windows guards Documents) and saves itself.

### 1. A cube on Hiro

The first scene: a spinning cube and a word on the Hiro marker.

1. **File > New**. The Hierarchy has the **Main Camera** and one anchor, **Hiro (Marker)**. Its Inspector's
   **AR Marker** section shows the pattern (Hiro) and **Save the printable marker...**: save it and print it at 100%.
2. With **Hiro** selected: **GameObject > 3D Object > Cube**. In the Inspector, **Transform**: Position Y `0.15`, Scale
   `0.3 0.3 0.3` (a marker is 1 across). In the **Material**: pick a colour.
3. **Add Component > Animate**: Does **Spin**, Speed `0.5`.
4. Select **Hiro** again: **GameObject > AR > Words**. Position Y `0.6`; type `Hello` in its Words box; tick
   **Faces the phone**.
5. Press **▶ Play** (Ctrl+P): the browser opens with the webcam; hold the printed Hiro up to it.
6. **File > Build Settings...**, pick **Web**, **Build**: the folder is a website (upload it to GitHub Pages,
   Netlify or itch.io); `Play.bat` plays it on this PC.

### 2. The art gallery

A shadow box: a frame on the Kanji marker, a forest down inside the paper, a woman, god rays and butterflies.
The pictures and the frame are in Diorama's `templates/gallery` folder (drag them into the Project window).

1. **File > New**, select the anchor, rename it `Gallery`; **AR Marker**: Pattern **Kanji**.
2. **Window into the marker:** tick it, Opening `1.5 1.5`: under the paper only shows through the opening.
3. **GameObject > Create Empty**, name it `Marker scene`, Scale `0.8 0.8 0.8`. Everything else goes under it
   (select it before creating, or drag onto it).
4. **GameObject > AR > Picture...** `tex_treesBG.png`: name `BG`, Position `0 -0.418 0`, Scale `1.9155` (all three).
   Pictures lie flat, facing up: this is the forest floor seen from above, deep in the box.
5. Two more pictures, `tex_godrays.png`: Position `0 -0.189 0` and `0.116 -0.175 0`, same scale; Material colour
   alpha `0.8` and `0.6`, tick **Glow**; **Add Component > Animate**: **Pulse**, Speed `0.4` / `0.3`, Amount
   `0.25` / `0.3`.
6. **Picture...** `tex_woman00.png`: name `Woman`, Position `0.288 -0.118 -0.278`, Scale `1.3644`; Animate **Float**,
   Speed `0.5`, Amount `0.015`.
7. **GameObject > AR > Model...** `obj_ar_frame.glb`: name `Frame`, Material colour a brown (`#7C2B00`).
8. **GameObject > AR > Sprites...** `tex_butterfly.png`: name `Butterflies`, Position `0 0.15 0`, Scale `1.6 1.1 1.1`;
   How many `10`, Size `0.22`, Flapping `1`, Speed `0.6`. They fly round inside the object's box.
9. **Lighting** on the anchor: the sun's colour, strength and direction light the frame.
10. **▶ Play** with the printed Kanji.

### 3. Plane detection: tap to place cubes

Planes found on floors, tables and walls; a tap on one puts a cube there.

1. **File > New**; **Edit > Project Settings**: What the game tracks: **Surfaces**. The Scene view becomes a pretend
   room (a floor, a table, a wall) with the planes a phone would find, dotted.
2. Select the anchor (rename it `Room`). **AR Plane Manager**: Detection **Both**, **Show the planes found**,
   Placement **On the first floor found**. Under **Any camera (no ARCore)**: Surface **Auto**, Looks down `50`,
   Height `0.6 m` (a webcam's height above the table: it sets the scale).
3. **GameObject > 3D Object > Cube**: Scale `0.1` (10 cm: Surfaces are in metres), Position Y `0.05`. This is the cube
   the script copies.
4. **GameObject > Create Empty**, name it `Spawner`; **Add Component > Script > New Script** (it's named after the
   object: `Spawner.js`; it opens in the Script Editor) and write:

   ```js
   var cubes = [];
   function start() {
     find("Cube").active = false;          // the cube to copy, hidden itself
     this.clear = function () { cubes.forEach(function (c) { destroy(c); }); cubes = []; };
   }
   function update(dt) {
     if (!input.tapped) return;
     var hits = planes.raycast(input.x, input.y);      // where the tap meets a plane
     if (!hits.length) return;
     var p = hits[0].position;
     var c = spawn("Cube", { worldPosition: { x: p.x, y: p.y + 0.05, z: p.z } });
     c.active = true;
     c.rotation.y = Math.random() * 360;
     c.getComponent("Material").colour.set(Math.random(), Math.random(), Math.random());
     cubes.push(c);
   }
   ```

5. **GameObject > UI > Button** (a Canvas is made for it): Text `Clear`, Anchor top right, and in **On Click**:
   `find('Spawner').clear()`.
6. **▶ Play**. On a PC: point the webcam at a table with some pattern (green dots show what it sees), **Lock**, then
   tap the table. On an Android phone with ARCore (Chrome): **Start AR**, move slowly until the dots appear, tap.

### 4. Indoor navigation

After "Augmented Reality in Indoor Navigation" (IJSREM, 2023): plane detection, path marking, path following.

1. A Surfaces project as in tutorial 3; anchor `Floor`, Placement **On the first floor found**.
2. **Create Empty** `Route`; **Add Component > XR > AR Path**: Name `Path 1`, Arrows every `0.5 m`, a colour, and
   points along the floor (each Point row is x y z in metres; right-click a point to remove it, **Add point** for more).
3. **GameObject > 3D Object > Capsule** `Guide`: Scale `0.2`, Position Y `0.2` (its Position is an offset from the
   path); **Add Component > XR > AR Path Follower**: Path `Path 1`, Speed `0.6 m/s`, Loop.
4. **GameObject > AR > Words** `Sign`: `Meeting room` at the route's end, **Faces the phone**.
5. The buttons (**GameObject > UI > Button**, each with an **On Click** line):
   - `Follow Path 1`: `nav.follow('Path 1')`
   - `Save Path 1`: `button.getComponent('Button').text = nav.toggle('Path 1') === 'recording' ? 'Stop + save' : 'Save Path 1'`
     (tap, walk the way, tap again: the walked path replaces the drawn one and is kept on the phone)
   - `Reset`: `nav.reset()`
6. **Build Settings > Android**, **Build And Run** with the phone on USB (ARCore phones walk whole rooms; PCs lock a
   table).

## Scripting reference

Scripts are JavaScript files on objects (Add Component > Script). A script's functions are called by name; `this` (and
`gameObject`) is its object. Click a script in the Inspector to edit it; **▶ Play** shows `log()` and errors in the
Console.

### Hooks

| Function | When |
|---|---|
| `start()` | once, before its first `update` |
| `update(dt)` | every frame; `dt` seconds since the last |
| `onTap()` | the player tapped this object |
| `onFound()` / `onLost()` | its anchor (marker, picture, face, surface) was found / lost |
| `onClick()` | a Button this script is on was pressed |
| `onChange(value)` | a Slider or Toggle this script is on changed |
| `onPlanesChanged(e)` | planes found / changed / gone: `e.added`, `e.updated`, `e.removed` (each `{ id, alignment, center, size, boundary }`) |
| `onArrived(path)` | an AR Path Follower on this object reached its path's end |

### Everywhere

| Name | What |
|---|---|
| `find("Name")`, `find("Parent/Child")`, `find("Hiro/Cube")`, `find("Main Camera/Torch")` | an object (or `null`) |
| `spawn("Name" or prefab, { position } or { worldPosition })` | a copy of an object (prefabs: `assets/<Name>.prefab`) and everything under it |
| `destroy(obj)` | removes it and what's under it |
| `log(...)` | to the Console |
| `time()` | seconds since the game started |
| `camera.position` | where the phone is `{ x, y, z }` |
| `input.tapped`, `input.x`, `input.y` | a tap this frame, where (-1..1 across the screen) |
| `loadScene("Name")`, `loadScene(1)` | another scene (scene 0 is `index.html`) |
| `scene.name`, `scene.index`, `scene.count`, `scene.names` | this scene |
| `planes.list`, `planes.raycast(x, y)` | the planes found; where a screen point meets them: `[{ position, distance, plane }]` |
| `nav.record(name)`, `nav.save(name)`, `nav.toggle(name)`, `nav.follow(name)`, `nav.stopFollowing()`, `nav.reset()`, `nav.paths()`, `nav.points(name)`, `nav.forget(name)`, `nav.recording` | walked paths (path marking and following) |

### An object

`obj.name`, `obj.position` / `obj.rotation` (degrees) / `obj.scale` (each `.x .y .z`, `.set(x, y, z)`, `.toArray()`),
`obj.active`, `obj.velocity` (moves it each frame), `obj.toCamera()` (direction to the phone and `.distance`),
`obj.lookAtCamera()`, `obj.find("Child")`, `obj.getComponent(type)`.

| `getComponent(...)` | Gives |
|---|---|
| `"Material"` | `colour` (`.set(r, g, b)`, 0..1), `opacity` |
| `"Animate"` | `on`, `does` ("Float", "Spin", "Sway", "Pulse", "Still"), `speed`, `amount` |
| `"Words"` | `text` |
| `"Model"` | `clip`, `play("Clip name")` |
| `"Light"` | `on`, `colour`, `intensity`, `range` |
| `"AudioSource"` | `play()`, `pause()`, `stop()`, `playing`, `volume`, `loop` |
| `"VideoPlayer"` | `play()`, `pause()`, `stop()`, `playing`, `time`, `duration`, `speed`, `volume`, `muted`, `element` |
| `"ParticleSystem"` | `on`, `speed`, `gravity`, `colourStart`, `colourEnd` |
| `"LineRenderer"` | `on`, `width`, `colour`, `point(i).set(x, y, z)`, `count` |
| `"TrailRenderer"` | `on`, `time`, `width`, `clear()` |
| `"Canvas"` | `visible`, `element` |
| `"Text"`, `"Button"`, `"Slider"`, `"Toggle"`, `"Image"`, `"Panel"` | `text`, `value`, `isOn`, `picture`, `on`, `position`, `colour`, `onClick(fn)`, `onChange(fn)`, `element` (the HTML) |
| a script's name | that script's object (what it put on `this`, e.g. `this.clear`) |

**Button On Click** (in the Inspector) is one line of script with `nav`, `find`, `loadScene`, `log` and `button`
(the button itself): `find('Spawner').clear()`, `loadScene('Level 2')`.

### Units

Markers and pictures are 1 across; faces are 1 wide; Surfaces are in metres. Y is up; rotations are in degrees.

## Install

Download `Diorama-win64.zip` from the [releases](https://github.com/CursedPrograms/Diorama/releases), unzip it and
run `install.bat`, or install it from the Atelier launcher.

## Build

Needs Visual Studio 2022 (C++) and CMake; SDL2, Dear ImGui and Microsoft's WebView2 SDK are fetched once. OpenCV 4.10
(optional: the native surface tracker and `tools/surfacetrack`) is found from `OpenCV_DIR`.

```bat
build.bat      :: build\Release, dist\Diorama and dist\Diorama-win64.zip
install.bat    :: installs dist\Diorama for this user
```

On Linux: `./build.sh` (cmake, a C++17 compiler, libsdl2-dev) and `./install.sh`. Android builds need the Android
SDK and Java 17 (Android Studio has both).

## Credits

Games are built on [three.js](https://threejs.org) (MIT), [jsartoolkit5](https://github.com/artoolkitx/jsartoolkit5)
(LGPL-3.0), [MindAR](https://github.com/hiukim/mind-ar-js) (MIT) and [OpenCV.js](https://opencv.org) (Apache 2.0),
shipped with their licences; Windows builds run on Microsoft's WebView2. Diorama itself uses
[Dear ImGui](https://github.com/ocornut/imgui), [SDL2](https://www.libsdl.org), [cgltf](https://github.com/jkuhlmann/cgltf),
[stb](https://github.com/nothings/stb), [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) and,
for the native tracker, [OpenCV](https://opencv.org). The surface tracker follows
[AR-Plane-Detection](https://github.com/CursedPrograms/AR-Plane-Detection); indoor navigation follows "Augmented Reality
in Indoor Navigation" (IJSREM, 2023). The art gallery's pictures and frame are from
[ARToolkit-ArtGallery-Template](https://github.com/CursedPrograms/ARToolkit-ArtGallery-Template). See [NOTICE](NOTICE).

<br>
<div align="center">
<a href="https://cursedprograms.github.io/Diorama/" target="_blank">
    <img src="https://raw.githubusercontent.com/CursedPrograms/Diorama/main/images/site-qr.png"
        alt="QR code to the Diorama website" style="width:200px;">
</a>
</div>

---
<br>
<div align="center">
<a href="https://github.com/Farica-Kimora">Farica☆Kimora</a> © Cursed Entertainment 2026
</div>
<br>
<div align="center">
<a href="https://cursed-entertainment.itch.io/" target="_blank">
    <img src="https://github.com/CursedPrograms/cursedentertainment/raw/main/images/logos/logo-wide-grey.png"
        alt="CursedEntertainment Logo" style="width:250px;">
</a>
</div>
