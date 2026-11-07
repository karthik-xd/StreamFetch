package com.streamfetch.app

import android.annotation.SuppressLint
import android.content.Context
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Loads the YouTube embed page in an off-screen WebView.
 * YouTube's own JavaScript fetches the video stream URLs — we intercept them.
 * No API keys, no PO tokens, no cipher decryption needed.
 */
object WebViewExtractor {

    // itag number → human-readable quality label
    private val ITAG_QUALITY = mapOf(
        "17" to "144p (3GP)",
        "18" to "360p",
        "22" to "720p",
        "37" to "1080p",
        "133" to "240p (video-only)",
        "134" to "360p (video-only)",
        "135" to "480p (video-only)",
        "136" to "720p (video-only)",
        "137" to "1080p (video-only)",
        "160" to "144p (video-only)",
        "242" to "240p WebM (video-only)",
        "243" to "360p WebM (video-only)",
        "244" to "480p WebM (video-only)",
        "247" to "720p WebM (video-only)",
        "248" to "1080p WebM (video-only)",
        "278" to "144p WebM (video-only)"
    )

    @SuppressLint("SetJavaScriptEnabled")
    suspend fun extract(
        context: Context,
        videoId: String,
        timeoutMs: Long = 20_000L
    ): Pair<String, List<VideoOption>> {

        // Thread-safe set so interceptor (background thread) can write safely
        val captured = LinkedHashSet<String>()
        val signal   = CompletableDeferred<Unit>()

        // WebView MUST be created and used on the main thread
        withContext(Dispatchers.Main) {
            val wv = WebView(context)
            wv.settings.apply {
                javaScriptEnabled              = true
                domStorageEnabled              = true
                mediaPlaybackRequiresUserGesture = false   // allow autoplay
                userAgentString =
                    "Mozilla/5.0 (Linux; Android 12; SM-N975F) " +
                    "AppleWebKit/537.36 (KHTML, like Gecko) " +
                    "Chrome/124.0.0.0 Mobile Safari/537.36"
            }

            wv.webViewClient = object : WebViewClient() {
                // Called for EVERY network request the WebView makes
                override fun shouldInterceptRequest(
                    view: WebView,
                    request: WebResourceRequest
                ): WebResourceResponse? {
                    val url = request.url.toString()
                    if (isVideoStream(url)) {
                        synchronized(captured) {
                            if (captured.add(url) && captured.size >= 2 && !signal.isCompleted) {
                                signal.complete(Unit)   // wake up the coroutine
                            }
                        }
                    }
                    return null   // null = let WebView fetch it normally
                }
            }

            // autoplay=1 mute=1 → player starts immediately without user tap
            // playsinline=1  → no fullscreen takeover on mobile
            wv.loadUrl(
                "https://www.youtube.com/embed/$videoId" +
                "?autoplay=1&mute=1&hl=en&playsinline=1&controls=0"
            )

            withTimeoutOrNull(timeoutMs) { signal.await() }

            wv.stopLoading()
            wv.destroy()
        }

        if (captured.isEmpty()) {
            throw Exception(
                "WebView found no stream URLs within ${timeoutMs / 1000}s. " +
                "Video may be private, age-restricted, or region-blocked."
            )
        }

        val opts = captured.mapNotNull { url ->
            val itag    = Regex("itag=(\\d+)").find(url)?.groupValues?.get(1) ?: return@mapNotNull null
            val quality = ITAG_QUALITY[itag] ?: "Stream (itag=$itag)"
            val mime    = Regex("mime=([^&]+)").find(url)?.groupValues?.get(1)
                ?.replace("%2F", "/")?.replace("%3B", ";") ?: "video/mp4"
            val isAudio = itag.toIntOrNull()?.let { it in 139..141 || it in 171..172 || it == 251 } ?: false
            if (isAudio) return@mapNotNull null   // skip audio-only
            VideoOption(
                format      = if (mime.contains("mp4")) "MP4" else "WebM",
                resolution  = quality,
                url         = url,
                isVideoOnly = quality.contains("video-only"),
                mimeType    = mime
            )
        }.sortedWith(compareBy({ it.isVideoOnly }, { -resInt(it.resolution) }))

        // Try to get the real title from oEmbed (lightweight, always works for public videos)
        val title = fetchTitle(videoId)

        return Pair(title, opts.ifEmpty {
            throw Exception("WebView intercepted requests but none were video streams")
        })
    }

    private fun isVideoStream(url: String): Boolean =
        (url.contains("googlevideo.com") || url.contains("videoplayback")) &&
        url.contains("itag=") &&
        !url.contains("mime=audio")   // skip audio-only adaptive streams

    private fun fetchTitle(videoId: String): String = try {
        val conn = java.net.URL(
            "https://www.youtube.com/oembed?url=https://www.youtube.com/watch?v=$videoId&format=json"
        ).openConnection() as java.net.HttpURLConnection
        conn.connectTimeout = 5_000
        conn.readTimeout    = 5_000
        val body = conn.inputStream.bufferedReader().readText()
        org.json.JSONObject(body).optString("title", "video")
    } catch (_: Exception) { "video" }

    private fun resInt(r: String) = r.filter { it.isDigit() }.take(4).toIntOrNull() ?: 0
}
