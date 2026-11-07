package com.streamfetch.app

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.documentfile.provider.DocumentFile
import kotlin.coroutines.coroutineContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.OutputStream
import java.util.concurrent.TimeUnit

object FileDownloader {

    private val client = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .writeTimeout(30, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    private const val CHUNK_SIZE = 2 * 1024 * 1024L // 2MB range chunks bypass YouTube 0.7MB/s rate throttling
    private const val BUFFER_SIZE = 128 * 1024 // 128KB disk write buffer

    suspend fun download(
        context: Context,
        fileUrl: String,
        audioUrl: String? = null,
        rawTitle: String,
        formatExtension: String = "mp4",
        isSliceEnabled: Boolean = false,
        sliceStartSec: Int = 0,
        sliceEndSec: Int = 0,
        onProgress: (Int) -> Unit = {},
        onProgressInfo: ((DownloadProgressInfo) -> Unit)? = null
    ): Boolean = withContext(Dispatchers.IO) {
        val sanitizedTitle = rawTitle.replace("[^a-zA-Z0-9._-]".toRegex(), "_")
        val ext = formatExtension.lowercase()
        val sliceSuffix = if (isSliceEnabled) "_slice_${formatTime(sliceStartSec)}-${formatTime(sliceEndSec)}".replace(":", "m") else ""
        val fileName = "$sanitizedTitle$sliceSuffix.$ext"
        val mimeType = when (ext) {
            "m4a" -> "audio/mp4"
            "opus", "ogg" -> "audio/ogg"
            "webm" -> if (fileUrl.contains("mime=audio")) "audio/webm" else "video/webm"
            else -> "video/mp4"
        }

        DownloadNotificationHelper.createNotificationChannel(context)

        fun report(info: DownloadProgressInfo) {
            onProgress(info.percentage)
            onProgressInfo?.invoke(info)
            DownloadNotificationHelper.updateProgress(context, rawTitle, info)
        }

        // Case 1: Separate video + audio tracks that need native muxing into full video
        if (audioUrl != null && (ext == "mp4" || ext == "webm")) {
            val audioExt = if (ext == "webm") "opus" else "m4a"
            val tempVideo = File(context.cacheDir, "temp_v_${System.currentTimeMillis()}.$ext")
            val tempAudio = File(context.cacheDir, "temp_a_${System.currentTimeMillis()}.$audioExt")
            val tempMuxed = File(context.cacheDir, "temp_m_${System.currentTimeMillis()}.$ext")
            val tempTrimmed = File(context.cacheDir, "temp_t_${System.currentTimeMillis()}.$ext")

            try {
                // Probe lengths of both streams
                val vTotal = getContentLength(fileUrl)
                val aTotal = getContentLength(audioUrl)
                val grandTotal = if (vTotal > 0 && aTotal > 0) vTotal + aTotal else 0L
                val tracker = SpeedTracker(grandTotal)

                // 1. Download video stream
                val vSuccess = downloadFileToDisk(
                    url = fileUrl,
                    destFile = tempVideo,
                    totalBytes = vTotal,
                    offsetBytes = 0L,
                    grandTotalBytes = grandTotal
                ) { currentDownloaded ->
                    report(tracker.getInfo(currentDownloaded, stage = "Downloading Video"))
                }
                if (!vSuccess) {
                    DownloadNotificationHelper.cancel(context)
                    return@withContext false
                }

                // 2. Download audio stream
                val vActualSize = tempVideo.length()
                val aSuccess = downloadFileToDisk(
                    url = audioUrl,
                    destFile = tempAudio,
                    totalBytes = aTotal,
                    offsetBytes = vActualSize,
                    grandTotalBytes = grandTotal
                ) { currentDownloaded ->
                    report(tracker.getInfo(currentDownloaded, stage = "Downloading Audio"))
                }
                if (!aSuccess) {
                    DownloadNotificationHelper.cancel(context)
                    return@withContext false
                }

                // 3. Muxing video and audio tracks
                report(DownloadProgressInfo(percentage = 92, isMuxing = true, stage = "Muxing Media"))
                val muxSuccess = MediaMuxerHelper.mux(tempVideo, tempAudio, tempMuxed)
                var finalSource = if (muxSuccess && tempMuxed.length() > 0) tempMuxed else tempVideo

                // 4. Trimming if slice enabled
                if (isSliceEnabled && sliceEndSec > sliceStartSec) {
                    report(DownloadProgressInfo(percentage = 96, isTrimming = true, stage = "Trimming Clip"))
                    val trimSuccess = MediaTrimmer.trimMp4(finalSource, tempTrimmed, sliceStartSec, sliceEndSec)
                    if (trimSuccess && tempTrimmed.length() > 0) {
                        finalSource = tempTrimmed
                    }
                }

                // 5. Save to destination
                report(DownloadProgressInfo(percentage = 98, stage = "Saving File"))
                val saveSuccess = writeStreamToDestination(context, fileName, mimeType) { out ->
                    FileInputStream(finalSource).use { it.copyTo(out) }
                }

                if (saveSuccess) {
                    val finalInfo = tracker.getInfo(grandTotal, stage = "Completed")
                    report(DownloadProgressInfo(percentage = 100, downloadedBytes = grandTotal, totalBytes = grandTotal, stage = "Complete"))
                    DownloadNotificationHelper.showComplete(context, rawTitle, finalInfo.totalFormatted)
                } else {
                    DownloadNotificationHelper.cancel(context)
                }
                return@withContext saveSuccess
            } catch (e: Exception) {
                e.printStackTrace()
                DownloadNotificationHelper.cancel(context)
                return@withContext false
            } finally {
                tempVideo.delete()
                tempAudio.delete()
                tempMuxed.delete()
                tempTrimmed.delete()
            }
        }

        // Case 2: Pre-muxed MP4 with slice enabled
        if (isSliceEnabled && ext == "mp4" && sliceEndSec > sliceStartSec) {
            val tempRawFile = File(context.cacheDir, "temp_raw_${System.currentTimeMillis()}.mp4")
            val tempTrimmedFile = File(context.cacheDir, "temp_trimmed_${System.currentTimeMillis()}.mp4")

            try {
                val totalBytes = getContentLength(fileUrl)
                val tracker = SpeedTracker(totalBytes)

                val dlSuccess = downloadFileToDisk(
                    url = fileUrl,
                    destFile = tempRawFile,
                    totalBytes = totalBytes,
                    offsetBytes = 0L,
                    grandTotalBytes = totalBytes
                ) { currentDownloaded ->
                    report(tracker.getInfo(currentDownloaded, stage = "Downloading"))
                }
                if (!dlSuccess) {
                    DownloadNotificationHelper.cancel(context)
                    return@withContext false
                }

                report(DownloadProgressInfo(percentage = 92, isTrimming = true, stage = "Trimming Clip"))
                val trimSuccess = MediaTrimmer.trimMp4(tempRawFile, tempTrimmedFile, sliceStartSec, sliceEndSec)
                val finalSource = if (trimSuccess && tempTrimmedFile.length() > 0) tempTrimmedFile else tempRawFile

                report(DownloadProgressInfo(percentage = 97, stage = "Saving File"))
                val saveSuccess = writeStreamToDestination(context, fileName, mimeType) { out ->
                    FileInputStream(finalSource).use { it.copyTo(out) }
                }

                if (saveSuccess) {
                    val finalInfo = tracker.getInfo(totalBytes, stage = "Completed")
                    report(DownloadProgressInfo(percentage = 100, downloadedBytes = totalBytes, totalBytes = totalBytes, stage = "Complete"))
                    DownloadNotificationHelper.showComplete(context, rawTitle, finalInfo.totalFormatted)
                } else {
                    DownloadNotificationHelper.cancel(context)
                }
                return@withContext saveSuccess
            } catch (e: Exception) {
                e.printStackTrace()
                DownloadNotificationHelper.cancel(context)
                return@withContext false
            } finally {
                tempRawFile.delete()
                tempTrimmedFile.delete()
            }
        }

        // Case 3: Direct single stream download
        val totalBytes = getContentLength(fileUrl)
        val tracker = SpeedTracker(totalBytes)

        val tempDirectFile = File(context.cacheDir, "temp_direct_${System.currentTimeMillis()}.$ext")
        try {
            val dlSuccess = downloadFileToDisk(
                url = fileUrl,
                destFile = tempDirectFile,
                totalBytes = totalBytes,
                offsetBytes = 0L,
                grandTotalBytes = totalBytes
            ) { currentDownloaded ->
                report(tracker.getInfo(currentDownloaded, stage = "Downloading"))
            }
            if (!dlSuccess) {
                DownloadNotificationHelper.cancel(context)
                return@withContext false
            }

            report(DownloadProgressInfo(percentage = 98, stage = "Saving File"))
            val saveSuccess = writeStreamToDestination(context, fileName, mimeType) { out ->
                FileInputStream(tempDirectFile).use { it.copyTo(out) }
            }

            if (saveSuccess) {
                val finalInfo = tracker.getInfo(totalBytes, stage = "Completed")
                report(DownloadProgressInfo(percentage = 100, downloadedBytes = totalBytes, totalBytes = totalBytes, stage = "Complete"))
                DownloadNotificationHelper.showComplete(context, rawTitle, finalInfo.totalFormatted)
            } else {
                DownloadNotificationHelper.cancel(context)
            }
            return@withContext saveSuccess
        } catch (e: Exception) {
            e.printStackTrace()
            DownloadNotificationHelper.cancel(context)
            return@withContext false
        } finally {
            tempDirectFile.delete()
        }
    }

    private fun getContentLength(url: String): Long {
        return try {
            val probeReq = Request.Builder()
                .url(url)
                .header("User-Agent", "com.google.android.youtube/21.26.364 (Linux; U; Android 14; Pixel 8 Pro) gzip")
                .header("Origin", "https://www.youtube.com")
                .header("Referer", "https://www.youtube.com/")
                .header("Range", "bytes=0-0")
                .build()
            client.newCall(probeReq).execute().use { resp ->
                val cr = resp.header("Content-Range")
                if (cr != null && cr.contains("/")) {
                    cr.substringAfter("/").trim().toLongOrNull() ?: -1L
                } else {
                    resp.body?.contentLength() ?: -1L
                }
            }
        } catch (_: Exception) {
            -1L
        }
    }

    private suspend fun downloadFileToDisk(
        url: String,
        destFile: File,
        totalBytes: Long,
        offsetBytes: Long,
        grandTotalBytes: Long,
        onProgressUpdate: (downloadedBytes: Long) -> Unit
    ): Boolean {
        destFile.parentFile?.mkdirs()

        // If length is known and > CHUNK_SIZE, use 2MB range chunks to burst at max wire speed
        if (totalBytes > CHUNK_SIZE) {
            val chunkSuccess = downloadChunked(url, destFile, totalBytes, offsetBytes, onProgressUpdate)
            if (chunkSuccess) return true
        }

        // Fallback: direct stream download
        return downloadDirect(url, destFile, offsetBytes, onProgressUpdate)
    }

    private suspend fun downloadChunked(
        url: String,
        destFile: File,
        totalBytes: Long,
        offsetBytes: Long,
        onProgressUpdate: (downloadedBytes: Long) -> Unit
    ): Boolean {
        val outStream = BufferedOutputStream(FileOutputStream(destFile), BUFFER_SIZE)
        var downloaded = 0L
        val buffer = ByteArray(BUFFER_SIZE)

        try {
            while (downloaded < totalBytes) {
                if (!coroutineContext.isActive) {
                    outStream.close()
                    destFile.delete()
                    return false
                }
                val start = downloaded
                val end = minOf(start + CHUNK_SIZE - 1, totalBytes - 1)
                val chunkReq = Request.Builder()
                    .url(url)
                    .header("User-Agent", "com.google.android.youtube/21.26.364 (Linux; U; Android 14; Pixel 8 Pro) gzip")
                    .header("Origin", "https://www.youtube.com")
                    .header("Referer", "https://www.youtube.com/")
                    .header("Range", "bytes=$start-$end")
                    .build()

                val resp = client.newCall(chunkReq).execute()
                if (!resp.isSuccessful) {
                    resp.close()
                    outStream.close()
                    destFile.delete()
                    return false
                }
                val body = resp.body ?: run {
                    resp.close()
                    outStream.close()
                    destFile.delete()
                    return false
                }

                body.byteStream().use { input ->
                    var read: Int
                    while (input.read(buffer).also { read = it } != -1) {
                        if (!coroutineContext.isActive) {
                            outStream.close()
                            destFile.delete()
                            return false
                        }
                        outStream.write(buffer, 0, read)
                        downloaded += read
                        onProgressUpdate(offsetBytes + downloaded)
                    }
                }
                resp.close()
            }
            outStream.flush()
            outStream.close()
            return true
        } catch (e: Exception) {
            try { outStream.close() } catch (_: Exception) {}
            destFile.delete()
            return false
        }
    }

    private suspend fun downloadDirect(
        url: String,
        destFile: File,
        offsetBytes: Long,
        onProgressUpdate: (downloadedBytes: Long) -> Unit
    ): Boolean {
        val request = Request.Builder()
            .url(url)
            .header("User-Agent", "com.google.android.youtube/21.26.364 (Linux; U; Android 14; Pixel 8 Pro) gzip")
            .header("Origin", "https://www.youtube.com")
            .header("Referer", "https://www.youtube.com/")
            .build()

        val response = client.newCall(request).execute()
        if (!response.isSuccessful) {
            response.close()
            return false
        }

        val body = response.body ?: run {
            response.close()
            return false
        }

        return try {
            val outStream = BufferedOutputStream(FileOutputStream(destFile), BUFFER_SIZE)
            body.byteStream().use { input ->
                val buffer = ByteArray(BUFFER_SIZE)
                var bytesRead: Int
                var downloadedBytes = 0L
                while (input.read(buffer).also { bytesRead = it } != -1) {
                    if (!coroutineContext.isActive) {
                        outStream.close()
                        destFile.delete()
                        return false
                    }
                    outStream.write(buffer, 0, bytesRead)
                    downloadedBytes += bytesRead
                    onProgressUpdate(offsetBytes + downloadedBytes)
                }
                outStream.flush()
            }
            outStream.close()
            true
        } catch (e: Exception) {
            destFile.delete()
            false
        } finally {
            response.close()
        }
    }

    private fun writeStreamToDestination(
        context: Context,
        fileName: String,
        mimeType: String,
        writer: (OutputStream) -> Unit
    ): Boolean {
        val prefs = context.getSharedPreferences("streamfetch_prefs", Context.MODE_PRIVATE)
        val customFolderUriString = prefs.getString("custom_folder_uri", null)

        val outputStream: OutputStream? = if (customFolderUriString != null) {
            val treeUri = Uri.parse(customFolderUriString)
            val pickedDir = DocumentFile.fromTreeUri(context, treeUri)
            val newFile = pickedDir?.createFile(mimeType, fileName)
            newFile?.uri?.let { context.contentResolver.openOutputStream(it) }
        } else {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val values = ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, fileName)
                    put(MediaStore.MediaColumns.MIME_TYPE, mimeType)
                    put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
                }
                val uri = context.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                uri?.let { context.contentResolver.openOutputStream(it) }
            } else {
                val downloadsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
                if (!downloadsDir.exists()) downloadsDir.mkdirs()
                val targetFile = File(downloadsDir, fileName)
                FileOutputStream(targetFile)
            }
        }

        return if (outputStream != null) {
            try {
                outputStream.use { writer(it) }
                true
            } catch (e: Exception) {
                e.printStackTrace()
                false
            }
        } else {
            false
        }
    }

    fun formatTime(seconds: Int): String {
        val m = seconds / 60
        val s = seconds % 60
        return "%02d:%02d".format(m, s)
    }

    private class SpeedTracker(private val totalBytes: Long) {
        private var lastTime = System.currentTimeMillis()
        private var lastBytes = 0L
        private var smoothedSpeed = 0L

        fun getInfo(currentDownloaded: Long, stage: String): DownloadProgressInfo {
            val now = System.currentTimeMillis()
            val dt = now - lastTime
            if (dt >= 250) {
                val dBytes = currentDownloaded - lastBytes
                val instSpeed = (dBytes * 1000L) / dt.coerceAtLeast(1)
                smoothedSpeed = if (smoothedSpeed == 0L) instSpeed else (smoothedSpeed * 6 + instSpeed * 4) / 10
                lastTime = now
                lastBytes = currentDownloaded
            }

            val eta = if (smoothedSpeed > 0 && totalBytes > currentDownloaded) {
                (totalBytes - currentDownloaded) / smoothedSpeed
            } else {
                0L
            }

            val percent = if (totalBytes > 0) {
                ((currentDownloaded * 100) / totalBytes).toInt().coerceIn(0, 99)
            } else 0

            return DownloadProgressInfo(
                percentage = percent,
                downloadedBytes = currentDownloaded,
                totalBytes = totalBytes,
                bytesPerSec = smoothedSpeed,
                etaSeconds = eta,
                stage = stage
            )
        }
    }
}
