// Diorama's page: the feature cards and the year
(function () {
  'use strict';
  const features = [
    ['Made like Unity', 'A Hierarchy, a Scene view and an Inspector; every object an empty with a Transform and components you add, switch off or remove.'],
    ['Unity\'s tools', 'Click to pick, Q W E R to move, rotate and scale with handles, F to frame, Ctrl+Z to undo; values drag up and down.'],
    ['Markers without a webcam', 'Hiro, Kanji, or a pattern made from any picture or letter, and 64 barcodes: printable, and a sheet of them all.'],
    ['Models that move', 'glTF and GLB models with their bones and animation clips, pictures, flying sprites, light rays and words.'],
    ['A window into the paper', 'Scenes can go down into the marker and only show through an opening, like a shadow-box gallery.'],
    ['Play it on a phone', 'Build Game makes a static website for any https host; Play opens it in your browser with the webcam.']
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
