// Projectile: flies away from the camera from where it starts, then is gone. Spawn copies of it (see Shooter).
var speed = 1.5;          // metres (or marker widths) a second
var life = 2;             // seconds
var age = 0;
function start() {
  var d = this.toCamera();
  this.velocity.set(-d.x * speed, -d.y * speed, -d.z * speed);
}
function update(dt) {
  age += dt;
  if (age >= life) destroy(this);
}
