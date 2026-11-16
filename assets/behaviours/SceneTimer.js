// SceneTimer: after some seconds, go to another scene (a title card, a level that ends).
var seconds = 10;
var nextScene = 1;        // a number (Build Settings' list) or a name
var t = 0;
function update(dt) {
  t += dt;
  if (t >= seconds) { t = -1e9; loadScene(nextScene); }
}
