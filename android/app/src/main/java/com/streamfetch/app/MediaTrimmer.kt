package com.streamfetch.app

import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import java.io.File
import java.nio.ByteBuffer

object MediaTrimmer {

    /**
     * Trims an MP4 file losslessly from [startSec] to [endSec] using Android native MediaMuxer.
     * Takes ~0.5 - 2 seconds because it does not re-encode.
     */
    fun trimMp4(inputFile: File, outputFile: File, startSec: Int, endSec: Int): Boolean {
        if (startSec < 0 || endSec <= startSec) return false
        val extractor = MediaExtractor()
        var muxer: MediaMuxer? = null
        try {
            extractor.setDataSource(inputFile.absolutePath)
            val trackCount = extractor.trackCount
            if (trackCount == 0) return false

            val isWebm = outputFile.name.lowercase().endsWith(".webm")
            val muxerFormat = if (isWebm) MediaMuxer.OutputFormat.MUXER_OUTPUT_WEBM else MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4
            muxer = MediaMuxer(outputFile.absolutePath, muxerFormat)
            val indexMap = HashMap<Int, Int>(trackCount)

            for (i in 0 until trackCount) {
                val format = extractor.getTrackFormat(i)
                val mime = format.getString(MediaFormat.KEY_MIME) ?: ""
                if (mime.startsWith("video/") || mime.startsWith("audio/")) {
                    extractor.selectTrack(i)
                    val dstIndex = muxer.addTrack(format)
                    indexMap[i] = dstIndex
                }
            }

            muxer.start()

            val startUs = startSec * 1_000_000L
            val endUs = endSec * 1_000_000L
            extractor.seekTo(startUs, MediaExtractor.SEEK_TO_CLOSEST_SYNC)

            val buffer = ByteBuffer.allocate(1024 * 1024)
            val info = MediaCodec.BufferInfo()

            while (true) {
                val trackIndex = extractor.sampleTrackIndex
                if (trackIndex < 0) break

                val sampleTime = extractor.sampleTime
                if (sampleTime > endUs) {
                    break
                }

                info.offset = 0
                info.size = extractor.readSampleData(buffer, 0)
                if (info.size < 0) break

                info.presentationTimeUs = (sampleTime - startUs).coerceAtLeast(0L)
                info.flags = extractor.sampleFlags

                val dstTrack = indexMap[trackIndex] ?: 0
                muxer.writeSampleData(dstTrack, buffer, info)

                extractor.advance()
            }
            return true
        } catch (e: Exception) {
            e.printStackTrace()
            return false
        } finally {
            try { muxer?.stop() } catch (_: Exception) {}
            try { muxer?.release() } catch (_: Exception) {}
            extractor.release()
        }
    }
}
