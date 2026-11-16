// Teleport: tap it and it jumps somewhere else nearby (a catch-me game).
var radius = 0.5;
function onTap() {
  var a = Math.random() * Math.PI * 2, r = radius * (0.4 + 0.6 * Math.random());
  this.position.set(Math.cos(a) * r, this.position.y, Math.sin(a) * r);
}
