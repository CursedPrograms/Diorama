// Shooter: a tap anywhere fires a copy of the object named below (give that one Projectile.js and hide it).
var projectile = "Bullet";
function start() {
  var p = find(projectile);
  if (p) p.active = false;
}
function update(dt) {
  if (!input.tapped) return;
  var b = spawn(projectile, { position: { x: this.position.x, y: this.position.y, z: this.position.z } });
  if (b) b.active = true;
}
