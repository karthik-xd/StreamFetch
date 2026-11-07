package com.streamfetch.app

import java.util.Locale

data class DownloadProgressInfo(
    val percentage: Int = 0,
    val downloadedBytes: Long = 0L,
    val totalBytes: Long = 0L,
    val bytesPerSec: Long = 0L,
    val etaSeconds: Long = 0L,
    val isMuxing: Boolean = false,
    val isTrimming: Boolean = false,
    val stage: String = ""
) {
    val downloadedFormatted: String
        get() = formatBytes(downloadedBytes)

    val totalFormatted: String
        get() = if (totalBytes > 0) formatBytes(totalBytes) else "--"

    val speedFormatted: String
        get() = "${formatBytes(bytesPerSec)}/s"

    val etaFormatted: String
        get() = when {
            etaSeconds <= 0 -> "--"
            etaSeconds < 60 -> "${etaSeconds}s"
            else -> "${etaSeconds / 60}m ${etaSeconds % 60}s"
        }

    companion object {
        fun formatBytes(bytes: Long): String {
            if (bytes <= 0) return "0 B"
            val kb = bytes / 1024.0
            val mb = kb / 1024.0
            val gb = mb / 1024.0
            return when {
                gb >= 1.0 -> String.format(Locale.US, "%.2f GB", gb)
                mb >= 1.0 -> String.format(Locale.US, "%.1f MB", mb)
                kb >= 1.0 -> String.format(Locale.US, "%.1f KB", kb)
                else -> "$bytes B"
            }
        }
    }
}
