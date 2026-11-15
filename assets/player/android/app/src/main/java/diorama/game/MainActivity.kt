package diorama.game

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.view.View
import android.view.WindowManager
import android.webkit.GeolocationPermissions
import android.webkit.MimeTypeMap
import android.webkit.PermissionRequest
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient

/**
 * A game made with Diorama: the game (the same as its website, in assets/game) full screen in a WebView.
 * It's served from an https address that never leaves the phone (https://appassets.androidplatform.net/),
 * so the browser engine treats it as secure and lets it use the camera; the camera (and the location and
 * microphone, when the game uses them) are asked for once, Android's way.
 */
class MainActivity : Activity() {
    private lateinit var web: WebView
    private var pending: PermissionRequest? = null
    private val host = "appassets.androidplatform.net"

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        web = WebView(this)
        web.setBackgroundColor(Color.rgb(17, 17, 17))
        setContentView(web)
        immersive()
        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            mediaPlaybackRequiresUserGesture = false
            setGeolocationEnabled(true)
            allowFileAccess = false
            allowContentAccess = false
        }
        web.webViewClient = object : WebViewClient() {
            // the game's files, from the app itself
            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
                val url = request.url
                if (url.host != host) return null
                var path = url.path ?: "/"
                if (path.endsWith("/")) path += "index.html"
                return try {
                    val stream = assets.open("game" + path)
                    val ext = path.substringAfterLast('.', "").lowercase()
                    val type = when (ext) {
                        "js", "mjs" -> "text/javascript"
                        "wasm" -> "application/wasm"
                        "glb" -> "model/gltf-binary"
                        "gltf" -> "model/gltf+json"
                        else -> MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
                    }
                    WebResourceResponse(type, if (type.startsWith("text/") || type.endsWith("json")) "utf-8" else null, stream)
                } catch (e: Exception) {
                    WebResourceResponse("text/plain", "utf-8", 404, "Not found", mapOf(), "Not found".byteInputStream())
                }
            }

            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                if (request.url.host == host) return false
                startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, request.url))   // links out: the real browser
                return true
            }
        }
        web.webChromeClient = object : WebChromeClient() {
            override fun onPermissionRequest(request: PermissionRequest) {
                runOnUiThread {
                    val needs = mutableListOf<String>()
                    if (request.resources.contains(PermissionRequest.RESOURCE_VIDEO_CAPTURE)) needs += Manifest.permission.CAMERA
                    if (request.resources.contains(PermissionRequest.RESOURCE_AUDIO_CAPTURE)) needs += Manifest.permission.RECORD_AUDIO
                    val missing = needs.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
                    if (missing.isEmpty()) request.grant(request.resources)
                    else { pending = request; requestPermissions(missing.toTypedArray(), 1) }
                }
            }

            override fun onGeolocationPermissionsShowPrompt(origin: String, callback: GeolocationPermissions.Callback) {
                if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) callback.invoke(origin, true, false)
                else { requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION), 2); callback.invoke(origin, true, false) }
            }
        }
        if (state == null) web.loadUrl("https://$host/") else web.restoreState(state)
    }

    override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {
        val p = pending ?: return
        pending = null
        if (results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) p.grant(p.resources) else p.deny()
    }

    private fun immersive() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_FULLSCREEN
            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) { super.onWindowFocusChanged(hasFocus); if (hasFocus) immersive() }
    override fun onSaveInstanceState(out: Bundle) { super.onSaveInstanceState(out); web.saveState(out) }
    override fun onPause() { super.onPause(); web.onPause() }
    override fun onResume() { super.onResume(); web.onResume() }
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() { if (web.canGoBack()) web.goBack() else super.onBackPressed() }
}
