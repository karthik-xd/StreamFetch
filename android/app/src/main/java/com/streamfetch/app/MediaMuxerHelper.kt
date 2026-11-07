package com.streamfetch.app

import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import java.io.File
import java.nio.ByteBuffer

object MediaMuxerHelper {

    /**
     * Muxes a video-only MP4 file and an audio-only M4A file into a single MP4 with both tracks.
     * Uses Android native MediaMuxer — no external libraries needed.
     */
    fun mux(videoFile: File, audioFile: File, outputFile: File): Boolean {
        val videoExtractor = MediaExtractor()
        val audioExtractor = MediaExtractor()
        var muxer: MediaMuxer? = null

        try {
            videoExtractor.setDataSource(videoFile.absolutePath)
            audioExtractor.setDataSource(audioFile.absolutePath)

            val isWebm = outputFile.name.lowercase().endsWith(".webm")
            val muxerFormat = if (isWebm) MediaMuxer.OutputFormat.MUXER_OUTPUT_WEBM else MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4
            muxer = MediaMuxer(outputFile.absolutePath, muxerFormat)

            // Select video track
            var videoDstIndex = -1
            for (i in 0 until videoExtractor.trackCount) {
                val format = videoExtractor.getTrackFormat(i)
                val mime = format.getString(MediaFormat.KEY_MIME) ?: ""
                if (mime.startsWith("video/")) {
                    videoExtractor.selectTrack(i)
                    videoDstIndex = muxer.addTrack(format)
                    break
                }
            }

            // Select audio track
            var audioDstIndex = -1
            for (i in 0 until audioExtractor.trackCount) {
                val format = audioExtractor.getTrackFormat(i)
                val mime = format.getString(MediaFormat.KEY_MIME) ?: ""
                if (mime.startsWith("audio/")) {
                    audioExtractor.selectTrack(i)
                    audioDstIndex = muxer.addTrack(format)
                    break
                }
            }

            if (videoDstIndex < 0 || audioDstIndex < 0) return false

            muxer.start()

            val buffer = ByteBuffer.allocate(1024 * 1024)
            val info = MediaCodec.BufferInfo()

            // Write video samples
            while (true) {
                info.offset = 0
                info.size = videoExtractor.readSampleData(buffer, 0)
                if (info.size < 0) break
                info.presentationTimeUs = videoExtractor.sampleTime
                info.flags = videoExtractor.sampleFlags
                muxer.writeSampleData(videoDstIndex, buffer, info)
                videoExtractor.advance()
            }

            // Write audio samples
            while (true) {
                info.offset = 0
                info.size = audioExtractor.readSampleData(buffer, 0)
                if (info.size < 0) break
                info.presentationTimeUs = audioExtractor.sampleTime
                info.flags = audioExtractor.sampleFlags
                muxer.writeSampleData(audioDstIndex, buffer, info)
                audioExtractor.advance()
            }

            return true
        } catch (e: Exception) {
            e.printStackTrace()
            return false
        } finally {
            try { muxer?.stop() } catch (_: Exception) {}
            try { muxer?.release() } catch (_: Exception) {}
            videoExtractor.release()
            audioExtractor.release()
        }
    }
}
