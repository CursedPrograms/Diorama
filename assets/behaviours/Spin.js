// Spin: turns steadily round its own axes (degrees a second).
var speed = { x: 0, y: 90, z: 0 };
function update(dt) {
  this.rotation.x += speed.x * dt; this.rotation.y += speed.y * dt; this.rotation.z += speed.z * dt;
}
