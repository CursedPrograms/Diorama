// Diorama's page: the feature cards and the year
(function () {
  'use strict';
  const features = [
    ['Made like Unity', 'A Hierarchy, a Scene view and an Inspector; every object an empty with a Transform and components you add, switch off or remove.'],
    ['Unity\'s tools', 'Click to pick, Q W E R to move, rotate and scale with handles, F to frame, Ctrl+Z to undo; values drag up and down.'],
    ['Markers without a webcam', 'Hiro, Kanji, or a pattern made from any picture or letter, and 64 barcodes: printable, and a sheet of them all.'],
    ['Models that move', 'glTF and GLB models with their bones and animation clips, pictures, flying sprites, light rays and words.'],
    ['A window into the paper', 'Scenes can go down into the marker and only show through an opening, like a shadow-box gallery.'],
    ['Plane detection', "Floors, tables and walls, as AR Foundation finds them: ARCore phones through WebXR, and any webcam with Diorama's own OpenCV tracker. Tap a plane to place things; indoor navigation paths with arrows."],
    ['ArUco markers', "OpenCV's square markers, 4x4 to 6x6: many on the table at once, each number its own model, printed from the editor."],
    ['Camera calibration', "Print the ChArUco board, show it to the game's Calibrate page from 15 angles: the lens's own numbers, so things sit exactly where they are."],
    ['Light like a PC game', 'PBR materials (metallic, roughness, glow), up to 16 lights, soft shadows onto the real table, HDR with bloom and filmic tone mapping.'],
    ['Video, UI and scenes', "A Video Player with see-through video, a Canvas of buttons and words, scenes like Unity's (scene 0 is the start), a Main Camera."],
    ['Build for everything', "Build Settings like Unity's: Web (WebGL or WebGPU), Windows, Linux, Raspberry Pi and Android, with Build And Run."],
    ['C++ and JavaScript together', 'Every tracker is written twice, the same steps: OpenCV.js in the browser game, OpenCV in C++ for the native player, tested against each other.']
  ];
  const grid = document.getElementById('features');
  if (grid) {
    for (const [title, text] of features) {
      const card = document.createElement('article');
      const h = document.createElement('h2');
      h.textContent = title;
      const p = document.createElement('p');
      p.textContent = text;
      card.append(h, p);
      grid.appendChild(card);
    }
  }
  const year = document.getElementById('year');
  if (year) year.textContent = String(Math.max(2026, new Date().getFullYear()));
})();
