// LookAtCameraY: turns round its upright axis to face the camera (signs, characters, trees drawn as pictures).
function update(dt) {
  var d = this.toCamera();
  this.rotation.y = Math.atan2(d.x, d.z) * 180 / Math.PI;
}
