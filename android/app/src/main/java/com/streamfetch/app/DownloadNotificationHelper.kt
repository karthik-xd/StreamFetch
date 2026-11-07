package com.streamfetch.app

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat

object DownloadNotificationHelper {

    private const val CHANNEL_ID = "streamfetch_download_channel"
    private const val CHANNEL_NAME = "StreamFetch Downloads"
    const val NOTIFICATION_ID = 1001

    fun createNotificationChannel(context: Context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Shows progress of active media downloads"
                setShowBadge(false)
            }
            val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    fun updateProgress(
        context: Context,
        title: String,
        info: DownloadProgressInfo
    ) {
        try {
            createNotificationChannel(context)

            val text = when {
                info.isMuxing -> "Muxing audio and video tracks..."
                info.isTrimming -> "Trimming video clip..."
                else -> "${info.downloadedFormatted} / ${info.totalFormatted} (${info.percentage}%) • ${info.speedFormatted} • ETA ${info.etaFormatted}"
            }

            val builder = NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle(title)
                .setContentText(text)
                .setProgress(100, info.percentage, info.percentage <= 0 && !info.isMuxing)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setPriority(NotificationCompat.PRIORITY_LOW)

            val manager = NotificationManagerCompat.from(context)
            manager.notify(NOTIFICATION_ID, builder.build())
        } catch (_: SecurityException) {
            // Notification permission denied on Android 13+
        } catch (_: Exception) {}
    }

    fun showComplete(context: Context, title: String, totalBytesFormatted: String) {
        try {
            createNotificationChannel(context)
            val builder = NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle("Download Complete")
                .setContentText("$title ($totalBytesFormatted)")
                .setAutoCancel(true)
                .setOngoing(false)
                .setPriority(NotificationCompat.PRIORITY_DEFAULT)

            val manager = NotificationManagerCompat.from(context)
            manager.notify(NOTIFICATION_ID, builder.build())
        } catch (_: Exception) {}
    }

    fun cancel(context: Context) {
        try {
            val manager = NotificationManagerCompat.from(context)
            manager.cancel(NOTIFICATION_ID)
        } catch (_: Exception) {}
    }
}
