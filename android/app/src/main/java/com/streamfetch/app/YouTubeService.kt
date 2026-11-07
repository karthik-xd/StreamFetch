package com.streamfetch.app

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.TimeUnit

enum class MediaFilterType {
    FULL_VIDEO,   // Video + Audio
    VIDEO_ONLY,   // Video only without audio
    AUDIO_ONLY    // High-quality audio
}

data class VideoOption(
    val format: String,        // "MP4", "WebM", "M4A", etc.
    val resolution: String,    // "1080p", "720p", "360p", "160 kbps", etc.
    val url: String,
    val isVideoOnly: Boolean,
    val isAudioOnly: Boolean = false,
    val mimeType: String = "",
    val bitrate: Int = 0,
    val audioUrl: String? = null // Non-null if it needs muxing with audio
)

data class ExtractionResult(
    val title: String,
    val durationSeconds: Int,
    val options: List<VideoOption>
)

object YouTubeService {

    private val http = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(20, TimeUnit.SECONDS)
        .writeTimeout(15, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    private const val PLAYER_URL = "https://www.youtube.com/youtubei/v1/player?prettyPrint=false"
    private const val JSON_MT = "application/json; charset=utf-8"

    private var cachedVisitorId: String? = null

    private data class ClientConfig(
        val label: String,
        val clientNum: String,
        val clientVersion: String,
        val userAgent: String,
        val jsonPayload: (videoId: String) -> String
    )

    private val CLIENTS = listOf(
        // 1. VisionOS with visitor token - delivers 1080p, 720p, 480p video-only AND high quality audio
        ClientConfig(
            label = "VISIONOS",
            clientNum = "101",
            clientVersion = "1.02",
            userAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15",
            jsonPayload = { vid ->
                """{"context":{"client":{"clientName":"VISIONOS","clientVersion":"1.02","deviceMake":"Apple","deviceModel":"RealityDevice17,1","userAgent":"Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15","osName":"visionOS","osVersion":"26.5.23O471","hl":"en","timeZone":"UTC","utcOffsetMinutes":0}},"videoId":"$vid","playbackContext":{"contentPlaybackContext":{"html5Preference":"HTML5_PREF_WANTS","signatureTimestamp":20760}},"contentCheckOk":true,"racyCheckOk":true}"""
            }
        ),

        // 2. Android Modern (Pixel 8 Pro) - direct 360p/720p muxed MP4 streams with audio
        ClientConfig(
            label = "ANDROID",
            clientNum = "3",
            clientVersion = "21.26.364",
            userAgent = "com.google.android.youtube/21.26.364 (Linux; U; Android 14; Pixel 8 Pro) gzip",
            jsonPayload = { vid ->
                """{"context":{"client":{"clientName":"ANDROID","clientVersion":"21.26.364","androidSdkVersion":34,"userAgent":"com.google.android.youtube/21.26.364 (Linux; U; Android 14; Pixel 8 Pro) gzip","osName":"Android","osVersion":"14","hl":"en","gl":"US"}},"videoId":"$vid","contentCheckOk":true,"racyCheckOk":true}"""
            }
        ),

        // 3. Android VR (Oculus Quest 3) - fallback
        ClientConfig(
            label = "ANDROID_VR",
            clientNum = "28",
            clientVersion = "1.65.10",
            userAgent = "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip",
            jsonPayload = { vid ->
                """{"context":{"client":{"clientName":"ANDROID_VR","clientVersion":"1.65.10","deviceMake":"Oculus","deviceModel":"Quest 3","androidSdkVersion":32,"userAgent":"com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip","osName":"Android","osVersion":"12L","hl":"en","gl":"US"}},"videoId":"$vid","contentCheckOk":true,"racyCheckOk":true}"""
            }
        )
    )

    fun videoId(url: String): String {
        val patterns = listOf(
            Regex("youtu\\.be/([a-zA-Z0-9_-]{11})"),
            Regex("[?&]v=([a-zA-Z0-9_-]{11})"),
            Regex("youtube\\.com/shorts/([a-zA-Z0-9_-]{11})"),
            Regex("youtube\\.com/embed/([a-zA-Z0-9_-]{11})"),
            Regex("youtube\\.com/live/([a-zA-Z0-9_-]{11})")
        )
        for (pattern in patterns) {
            pattern.find(url)?.let { return it.groupValues[1] }
        }
        val t = url.trim()
        if (t.length == 11 && t.all { it.isLetterOrDigit() || it == '_' || it == '-' }) return t
        throw IllegalArgumentException("No YouTube video ID found in: $url")
    }

    private fun getVisitorId(vid: String): String {
        cachedVisitorId?.let { return it }
        return try {
            val req = Request.Builder()
                .url("https://www.youtube.com/watch?v=$vid")
                .header("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
                .build()
            val resp = http.newCall(req).execute()
            val body = resp.body?.string() ?: ""
            resp.close()
            val match = Regex("\"VISITOR_DATA\":\"([^\"]+)\"").find(body)
            val id = match?.groupValues?.get(1) ?: ""
            if (id.isNotEmpty()) {
                cachedVisitorId = id
            }
            id
        } catch (_: Exception) {
            ""
        }
    }

    suspend fun extractStream(context: Context? = null, url: String): ExtractionResult =
        withContext(Dispatchers.IO) {
            val vid = videoId(url)
            val visitorId = getVisitorId(vid)
            val errors = mutableListOf<String>()
            var bestTitle = "video"
            var bestDuration = 0
            val allOptions = mutableListOf<VideoOption>()
            val seenUrls = mutableSetOf<String>()

            for (cfg in CLIENTS) {
                try {
                    val (title, duration, opts) = queryClient(vid, cfg, visitorId)
                    if (title.isNotEmpty() && bestTitle == "video") {
                        bestTitle = title
                    }
                    if (duration > 0 && bestDuration == 0) {
                        bestDuration = duration
                    }
                    for (opt in opts) {
                        if (seenUrls.add(opt.url)) {
                            allOptions.add(opt)
                        }
                    }
                } catch (e: Exception) {
                    errors += "${cfg.label}: ${e.message}"
                }
            }

            // Top audio streams for pairing full videos
            val topMp4Audio = allOptions.filter { it.isAudioOnly && it.format == "M4A" }
                .maxByOrNull { it.bitrate }
            val topWebmAudio = allOptions.filter { it.isAudioOnly && (it.format == "Opus" || it.format == "WebM") }
                .maxByOrNull { it.bitrate }

            // Existing pre-muxed resolutions (e.g. 360p from formats)
            val existingMuxedKeys = allOptions.filter { !it.isVideoOnly && !it.isAudioOnly }
                .map { "${it.resolution}_${it.format}" }.toSet()

            // 1. Generate Full Video options for MP4 video streams
            if (topMp4Audio != null) {
                val mp4VideoOnly = allOptions.filter { it.isVideoOnly && !it.isAudioOnly && it.format == "MP4" }
                val groupedByRes = mp4VideoOnly.groupBy { it.resolution }
                for ((res, streams) in groupedByRes) {
                    val key = "${res}_MP4"
                    if (!existingMuxedKeys.contains(key)) {
                        // Prefer avc1 (H.264) for maximum compatibility with MediaMuxer and hardware decoders
                        val bestStream = streams.find { it.mimeType.contains("avc1") } ?: streams.maxByOrNull { it.bitrate } ?: streams.first()
                        allOptions.add(
                            VideoOption(
                                format = "MP4",
                                resolution = res,
                                url = bestStream.url,
                                isVideoOnly = false,
                                isAudioOnly = false,
                                mimeType = "video/mp4",
                                bitrate = bestStream.bitrate + topMp4Audio.bitrate,
                                audioUrl = topMp4Audio.url
                            )
                        )
                    }
                }
            }

            // 2. Generate Full Video options for WebM video streams
            if (topWebmAudio != null) {
                val webmVideoOnly = allOptions.filter { it.isVideoOnly && !it.isAudioOnly && it.format == "WebM" }
                val groupedByRes = webmVideoOnly.groupBy { it.resolution }
                for ((res, streams) in groupedByRes) {
                    val key = "${res}_WebM"
                    if (!existingMuxedKeys.contains(key)) {
                        val bestStream = streams.maxByOrNull { it.bitrate } ?: streams.first()
                        allOptions.add(
                            VideoOption(
                                format = "WebM",
                                resolution = res,
                                url = bestStream.url,
                                isVideoOnly = false,
                                isAudioOnly = false,
                                mimeType = "video/webm",
                                bitrate = bestStream.bitrate + topWebmAudio.bitrate,
                                audioUrl = topWebmAudio.url
                            )
                        )
                    }
                }
            }

            if (allOptions.isNotEmpty()) {
                val uniqueOptions = allOptions.distinctBy { 
                    "${it.resolution}_${it.format}_${it.isVideoOnly}_${it.isAudioOnly}_${it.audioUrl != null}"
                }
                return@withContext ExtractionResult(bestTitle, bestDuration, uniqueOptions)
            }

            // Fallback to WebView if provided
            if (context != null) {
                try {
                    val (title, opts) = WebViewExtractor.extract(context, vid)
                    return@withContext ExtractionResult(title, bestDuration, opts)
                } catch (e: Exception) {
                    errors += "WebView: ${e.message}"
                }
            }

            throw Exception("Extraction failed:\n${errors.joinToString("\n")}")
        }

    private data class ClientQueryResult(
        val title: String,
        val duration: Int,
        val options: List<VideoOption>
    )

    private fun queryClient(vid: String, cfg: ClientConfig, visitorId: String): ClientQueryResult {
        val body = cfg.jsonPayload(vid).toRequestBody(JSON_MT.toMediaType())
        val reqBuilder = Request.Builder()
            .url(PLAYER_URL)
            .post(body)
            .header("Content-Type", JSON_MT)
            .header("X-YouTube-Client-Name", cfg.clientNum)
            .header("X-YouTube-Client-Version", cfg.clientVersion)
            .header("User-Agent", cfg.userAgent)
            .header("Origin", "https://www.youtube.com")
            .header("Referer", "https://www.youtube.com/watch?v=$vid")

        if (visitorId.isNotEmpty()) {
            reqBuilder.header("X-Goog-Visitor-Id", visitorId)
        }

        val resp = http.newCall(reqBuilder.build()).execute()
        val raw = resp.body?.string() ?: throw Exception("Empty response (HTTP ${resp.code})")
        if (!resp.isSuccessful) throw Exception("HTTP ${resp.code}: ${resp.message}")

        val json = JSONObject(raw)
        val ps = json.optJSONObject("playabilityStatus")
        val stat = ps?.optString("status") ?: "UNKNOWN"
        if (stat != "OK") {
            throw Exception("$stat: ${ps?.optString("reason") ?: stat}")
        }

        val vd = json.optJSONObject("videoDetails")
        val title = vd?.optString("title", "video") ?: "video"
        val duration = vd?.optString("lengthSeconds", "0")?.toIntOrNull() ?: 0
        val sd = json.optJSONObject("streamingData") ?: throw Exception("No streaming data")

        val opts = mutableListOf<VideoOption>()
        // 1. Muxed full video (audio + video)
        gatherMuxed(sd.optJSONArray("formats"), opts)
        // 2. Video only & Audio only adaptive formats
        gatherAdaptive(sd.optJSONArray("adaptiveFormats"), opts)

        return ClientQueryResult(title, duration, opts)
    }

    private fun gatherMuxed(arr: JSONArray?, out: MutableList<VideoOption>) {
        if (arr == null) return
        for (i in 0 until arr.length()) {
            val f = arr.getJSONObject(i)
            val streamUrl = f.optString("url", "")
            if (streamUrl.isEmpty()) continue
            val mime = f.optString("mimeType", "")
            val q = f.optString("qualityLabel", f.optString("quality", "Standard"))
            val bitrate = f.optInt("bitrate", 0)

            out += VideoOption(
                format = if (mime.contains("mp4")) "MP4" else "WebM",
                resolution = q,
                url = streamUrl,
                isVideoOnly = false,
                isAudioOnly = false,
                mimeType = mime,
                bitrate = bitrate
            )
        }
    }

    private fun gatherAdaptive(arr: JSONArray?, out: MutableList<VideoOption>) {
        if (arr == null) return
        for (i in 0 until arr.length()) {
            val f = arr.getJSONObject(i)
            val streamUrl = f.optString("url", "")
            if (streamUrl.isEmpty()) continue
            val mime = f.optString("mimeType", "")
            val bitrate = f.optInt("bitrate", 0)

            if (mime.startsWith("video/")) {
                val q = f.optString("qualityLabel", f.optString("quality", "Standard"))
                out += VideoOption(
                    format = if (mime.contains("mp4")) "MP4" else "WebM",
                    resolution = q,
                    url = streamUrl,
                    isVideoOnly = true,
                    isAudioOnly = false,
                    mimeType = mime,
                    bitrate = bitrate
                )
            } else if (mime.startsWith("audio/")) {
                val kbps = if (bitrate > 0) "${bitrate / 1000} kbps" else "High Quality"
                val audioFormat = when {
                    mime.contains("mp4") || mime.contains("m4a") -> "M4A"
                    mime.contains("opus") || mime.contains("webm") -> "Opus"
                    else -> "Audio"
                }
                out += VideoOption(
                    format = audioFormat,
                    resolution = kbps,
                    url = streamUrl,
                    isVideoOnly = false,
                    isAudioOnly = true,
                    mimeType = mime,
                    bitrate = bitrate
                )
            }
        }
    }
}
