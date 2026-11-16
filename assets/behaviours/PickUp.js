// PickUp: tap it to collect it: it's gone, and the Score goes up (a Canvas Text called "Score", if there is one).
var points = 1;
function onTap() {
  var s = find("Score");
  if (s) {
    var t = s.getComponent("Text");
    if (t) { var n = (parseInt(String(t.text).replace(/[^0-9-]/g, ""), 10) || 0) + points; t.text = "Score: " + n; }
  }
  destroy(this);
}
