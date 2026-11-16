// Waypoint: walks from point to point (its children's places, or a square), round and round.
var speed = 0.3;          // a second
var points = [];          // filled from the children named "Point..." if there are any
var at = 0;
function start() {
  if (!points.length) points = [{ x: 0.3, z: 0.3 }, { x: -0.3, z: 0.3 }, { x: -0.3, z: -0.3 }, { x: 0.3, z: -0.3 }];
}
function update(dt) {
  var p = points[at], dx = p.x - this.position.x, dz = p.z - this.position.z, d = Math.sqrt(dx * dx + dz * dz);
  if (d < 0.01) { at = (at + 1) % points.length; return; }
  var step = Math.min(d, speed * dt);
  this.position.x += dx / d * step; this.position.z += dz / d * step;
  this.rotation.y = Math.atan2(dx, dz) * 180 / Math.PI;
}
