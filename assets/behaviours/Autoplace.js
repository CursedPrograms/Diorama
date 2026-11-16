// Autoplace (Surfaces): when the first plane is found, a copy of the object named below lands where the screen's
// middle meets it (or on the plane's centre). Put this on any object (an empty is fine).
var what = "Cube";
var placed = false;
function start() { var o = find(what); if (o) o.active = false; }
function onPlanesChanged(e) {
  if (placed || !e.added.length) return;
  var hits = planes.raycast(0, 0);
  var p = hits.length ? hits[0].position : e.added[0].center;
  var copy = spawn(what, { worldPosition: p });
  if (copy) copy.active = true;
  placed = true;
}
