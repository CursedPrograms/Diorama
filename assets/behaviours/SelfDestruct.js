// SelfDestruct: removes this object (and what's under it) a while after it appears. Good on spawned things.
var seconds = 3;          // how long it lives
var age = 0;
function update(dt) {
  age += dt;
  if (age >= seconds) destroy(this);
}
