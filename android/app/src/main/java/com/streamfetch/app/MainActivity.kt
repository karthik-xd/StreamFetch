package com.streamfetch.app

import android.app.Activity
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.ImageDecoder
import android.graphics.drawable.AnimatedImageDrawable
import android.os.Build
import android.os.Bundle
import android.widget.ImageView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.streamfetch.app.ui.theme.StreamFetchTheme
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        var initialUrl = ""
        if (intent?.action == Intent.ACTION_SEND && intent.type == "text/plain") {
            intent.getStringExtra(Intent.EXTRA_TEXT)?.let { sharedText ->
                val extracted = extractUrl(sharedText)
                if (extracted.isNotEmpty()) initialUrl = extracted
            }
        }

        setContent {
            StreamFetchTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    MainScreen(initialUrl = initialUrl)
                }
            }
        }
    }

    private fun extractUrl(text: String): String {
        val regex = Regex("https?://[^\\s]+")
        return regex.find(text)?.value ?: ""
    }
}

@Composable
fun AnimatedDownloadButtonGraphic(
    modifier: Modifier = Modifier,
    isDownloading: Boolean = false,
    isSuccess: Boolean = false
) {
    if (isDownloading && Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
        AndroidView(
            modifier = modifier,
            factory = { ctx ->
                ImageView(ctx).apply {
                    scaleType = ImageView.ScaleType.FIT_CENTER
                    try {
                        val source = ImageDecoder.createSource(ctx.resources, R.drawable.download_animated)
                        val drawable = ImageDecoder.decodeDrawable(source)
                        setImageDrawable(drawable)
                        (drawable as? AnimatedImageDrawable)?.apply {
                            repeatCount = AnimatedImageDrawable.REPEAT_INFINITE
                            start()
                        }
                    } catch (_: Exception) {
                        setImageResource(R.mipmap.ic_launcher)
                    }
                }
            },
            update = { imageView ->
                try {
                    val drawable = imageView.drawable
                    (drawable as? AnimatedImageDrawable)?.apply {
                        repeatCount = AnimatedImageDrawable.REPEAT_INFINITE
                        start()
                    }
                } catch (_: Exception) {}
            }
        )
    } else if (isSuccess) {
        Box(
            modifier = modifier.background(Color.White),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "✓",
                color = Color(0xFF2EB886),
                fontSize = 20.sp,
                fontWeight = FontWeight.Bold
            )
        }
    } else {
        Image(
            painter = painterResource(id = R.mipmap.ic_launcher),
            contentDescription = "StreamFetch Icon",
            modifier = modifier
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(initialUrl: String) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()

    // Request notification permission on Android 13+
    val notifPermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { /* User handled notification permission */ }

    LaunchedEffect(Unit) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            notifPermissionLauncher.launch(android.Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    var urlInput by remember { mutableStateOf(initialUrl) }
    var isLoading by remember { mutableStateOf(false) }
    var videoTitle by remember { mutableStateOf("") }
    var videoDuration by remember { mutableIntStateOf(0) }
    var streamOptions by remember { mutableStateOf(listOf<VideoOption>()) }

    var selectedFilter by remember { mutableStateOf(MediaFilterType.FULL_VIDEO) }
    var selectedFormatFilter by remember { mutableStateOf("MP4") }
    var selectedResolution by remember { mutableStateOf("") }

    var enableSlice by remember { mutableStateOf(false) }
    var sliceRange by remember { mutableStateOf(0f..100f) }

    var isDownloading by remember { mutableStateOf(false) }
    var downloadProgress by remember { mutableIntStateOf(0) }
    var downloadInfo by remember { mutableStateOf<DownloadProgressInfo?>(null) }
    var statusMessage by remember { mutableStateOf("") }
    var downloadedOptionUrl by remember { mutableStateOf<String?>(null) }
    var downloadJob by remember { mutableStateOf<Job?>(null) }

    val mintGreen = MaterialTheme.colorScheme.primary
    val logoRed = MaterialTheme.colorScheme.secondary

    // Base options for current category
    val matchingCategoryOptions = remember(streamOptions, selectedFilter) {
        when (selectedFilter) {
            MediaFilterType.FULL_VIDEO -> {
                streamOptions.filter { !it.isVideoOnly && !it.isAudioOnly }
                    .sortedByDescending { it.resolution.filter { c -> c.isDigit() }.toIntOrNull() ?: 0 }
            }
            MediaFilterType.VIDEO_ONLY -> {
                streamOptions.filter { it.isVideoOnly && !it.isAudioOnly }
                    .sortedByDescending { it.resolution.filter { c -> c.isDigit() }.toIntOrNull() ?: 0 }
            }
            MediaFilterType.AUDIO_ONLY -> {
                streamOptions.filter { it.isAudioOnly }
                    .sortedByDescending { it.bitrate }
            }
        }
    }

    // Available formats for current category
    val availableFormats = remember(selectedFilter) {
        when (selectedFilter) {
            MediaFilterType.AUDIO_ONLY -> listOf("M4A", "Opus")
            else -> listOf("MP4", "WebM")
        }
    }

    // Filter by chosen format (MP4 vs WebM or M4A vs Opus)
    val formatFilteredOptions = remember(matchingCategoryOptions, selectedFormatFilter) {
        matchingCategoryOptions.filter { it.format.equals(selectedFormatFilter, ignoreCase = true) }
    }

    // Available resolutions/qualities for scroll of options
    val availableResolutions = remember(formatFilteredOptions) {
        formatFilteredOptions.map { it.resolution }.distinct()
    }

    // Auto-select highest quality on filter update
    LaunchedEffect(availableResolutions) {
        if (selectedResolution.isEmpty() || !availableResolutions.contains(selectedResolution)) {
            selectedResolution = availableResolutions.firstOrNull() ?: ""
        }
    }

    // The single active option chosen by user
    val activeOption = remember(formatFilteredOptions, selectedResolution) {
        formatFilteredOptions.firstOrNull { it.resolution.equals(selectedResolution, ignoreCase = true) }
            ?: formatFilteredOptions.firstOrNull()
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        // App Header: Logo + App Name + Exit Button
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
            modifier = Modifier.fillMaxWidth()
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Image(
                    painter = painterResource(id = R.mipmap.ic_launcher),
                    contentDescription = "StreamFetch Logo",
                    modifier = Modifier
                        .size(46.dp)
                        .clip(RoundedCornerShape(12.dp))
                )
                Spacer(modifier = Modifier.width(10.dp))
                Column {
                    Text(
                        text = "StreamFetch",
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onBackground
                    )
                    Text(
                        text = "Lightweight Native Media Grabber",
                        style = MaterialTheme.typography.bodySmall,
                        color = mintGreen
                    )
                }
            }

            // Exit Button
            OutlinedButton(
                onClick = { (context as? Activity)?.finishAffinity() },
                shape = RoundedCornerShape(10.dp),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = logoRed
                ),
                border = BorderStroke(1.dp, logoRed.copy(alpha = 0.5f)),
                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
            ) {
                Text("Exit", fontWeight = FontWeight.Bold)
            }
        }

        Spacer(modifier = Modifier.height(14.dp))

        // Storage Destination Settings
        DownloadLocationSettings()

        Spacer(modifier = Modifier.height(14.dp))

        // URL Input Row: Text Field + Red Paste Button
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            OutlinedTextField(
                value = urlInput,
                onValueChange = { urlInput = it },
                label = { Text("Video URL") },
                singleLine = true,
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier
                    .weight(1f)
                    .padding(end = 8.dp),
                trailingIcon = {
                    if (urlInput.isNotEmpty()) {
                        IconButton(onClick = { urlInput = "" }) {
                            Text("✕", color = MaterialTheme.colorScheme.outline)
                        }
                    }
                }
            )

            Button(
                onClick = {
                    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                    val clip = clipboard.primaryClip
                    if (clip != null && clip.itemCount > 0) {
                        val pasteText = clip.getItemAt(0).text?.toString() ?: ""
                        if (pasteText.isNotBlank()) {
                            urlInput = pasteText.trim()
                        }
                    }
                },
                colors = ButtonDefaults.buttonColors(
                    containerColor = logoRed,
                    contentColor = Color.White
                ),
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier.height(56.dp),
                contentPadding = PaddingValues(horizontal = 16.dp)
            ) {
                Text("Paste", fontWeight = FontWeight.Bold)
            }
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Fetch Streams Button (Mint Green Theme)
        Button(
            onClick = {
                if (urlInput.isBlank()) {
                    Toast.makeText(context, "Please enter a valid URL", Toast.LENGTH_SHORT).show()
                    return@Button
                }
                isLoading = true
                statusMessage = "Fetching formats..."
                coroutineScope.launch {
                    try {
                        val result = YouTubeService.extractStream(context, urlInput.trim())
                        videoTitle = result.title
                        videoDuration = result.durationSeconds
                        streamOptions = result.options
                        selectedFormatFilter = if (selectedFilter == MediaFilterType.AUDIO_ONLY) "M4A" else "MP4"
                        selectedResolution = ""
                        val maxDur = if (result.durationSeconds > 0) result.durationSeconds.toFloat() else 60f
                        sliceRange = 0f..maxDur
                        statusMessage = if (result.options.isEmpty()) "No downloadable streams found" else "Ready to download"
                    } catch (e: Exception) {
                        statusMessage = "Extraction error: ${e.localizedMessage ?: "Unknown error"}"
                    } finally {
                        isLoading = false
                    }
                }
            },
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = mintGreen,
                contentColor = MaterialTheme.colorScheme.onPrimary
            ),
            enabled = !isLoading && !isDownloading
        ) {
            Text(
                text = if (isLoading) "Extracting Streams..." else "Fetch Formats",
                fontWeight = FontWeight.SemiBold
            )
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Clean Download Status / Error message (when not downloading)
        if (!isDownloading && statusMessage.isNotEmpty()) {
            Text(
                text = statusMessage,
                style = MaterialTheme.typography.bodySmall,
                color = if (statusMessage.startsWith("Extraction error") || statusMessage.contains("failed")) logoRed else mintGreen,
                fontWeight = FontWeight.Medium
            )
        }

        // Once video formats are fetched:
        if (videoTitle.isNotEmpty()) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = videoTitle,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                maxLines = 2
            )

            // Video Duration & Slicer Card
            if (videoDuration > 0) {
                Spacer(modifier = Modifier.height(8.dp))
                ElevatedCard(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(12.dp),
                    colors = CardDefaults.elevatedCardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                    )
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "Video Slicer (Range Trimmer)",
                                style = MaterialTheme.typography.bodyMedium,
                                fontWeight = FontWeight.Bold
                            )
                            Switch(
                                checked = enableSlice,
                                onCheckedChange = { enableSlice = it },
                                colors = SwitchDefaults.colors(
                                    checkedThumbColor = Color.White,
                                    checkedTrackColor = mintGreen
                                )
                            )
                        }

                        if (enableSlice) {
                            Spacer(modifier = Modifier.height(4.dp))
                            RangeSlider(
                                value = sliceRange,
                                onValueChange = { sliceRange = it },
                                valueRange = 0f..videoDuration.toFloat(),
                                colors = SliderDefaults.colors(
                                    thumbColor = logoRed,
                                    activeTrackColor = mintGreen,
                                    inactiveTrackColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f)
                                )
                            )
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                    text = "Start: ${FileDownloader.formatTime(sliceRange.start.toInt())}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = mintGreen,
                                    fontWeight = FontWeight.Bold
                                )
                                Text(
                                    text = "Clip: ${FileDownloader.formatTime((sliceRange.endInclusive - sliceRange.start).toInt())}",
                                    style = MaterialTheme.typography.bodySmall,
                                    fontWeight = FontWeight.Bold
                                )
                                Text(
                                    text = "End: ${FileDownloader.formatTime(sliceRange.endInclusive.toInt())} / ${FileDownloader.formatTime(videoDuration)}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = logoRed,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // 1. Primary Category Tabs: Full Video | Video Only | Audio Only (Horizontally Scrollable)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilterChip(
                    selected = selectedFilter == MediaFilterType.FULL_VIDEO,
                    onClick = {
                        selectedFilter = MediaFilterType.FULL_VIDEO
                        selectedFormatFilter = "MP4"
                        selectedResolution = ""
                    },
                    label = { Text("Full Video", fontWeight = FontWeight.Bold) },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = mintGreen,
                        selectedLabelColor = Color.White
                    ),
                    shape = RoundedCornerShape(10.dp)
                )
                FilterChip(
                    selected = selectedFilter == MediaFilterType.VIDEO_ONLY,
                    onClick = {
                        selectedFilter = MediaFilterType.VIDEO_ONLY
                        selectedFormatFilter = "MP4"
                        selectedResolution = ""
                    },
                    label = { Text("Video only", fontWeight = FontWeight.Bold) },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = mintGreen,
                        selectedLabelColor = Color.White
                    ),
                    shape = RoundedCornerShape(10.dp)
                )
                FilterChip(
                    selected = selectedFilter == MediaFilterType.AUDIO_ONLY,
                    onClick = {
                        selectedFilter = MediaFilterType.AUDIO_ONLY
                        selectedFormatFilter = "M4A"
                        selectedResolution = ""
                    },
                    label = { Text("Audio only", fontWeight = FontWeight.Bold) },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = mintGreen,
                        selectedLabelColor = Color.White
                    ),
                    shape = RoundedCornerShape(10.dp)
                )
            }

            Spacer(modifier = Modifier.height(6.dp))

            // 2. Format Filter Row: MP4 | WebM (or M4A | Opus)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Format:",
                    style = MaterialTheme.typography.bodySmall,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.outline
                )
                availableFormats.forEach { fmt ->
                    FilterChip(
                        selected = selectedFormatFilter == fmt,
                        onClick = {
                            selectedFormatFilter = fmt
                            selectedResolution = ""
                        },
                        label = { Text(fmt) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = logoRed,
                            selectedLabelColor = Color.White
                        ),
                        shape = RoundedCornerShape(8.dp)
                    )
                }
            }

            // 3. Scroll of Options: Quality / Resolution Selector
            if (availableResolutions.isNotEmpty()) {
                Spacer(modifier = Modifier.height(6.dp))
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = if (selectedFilter == MediaFilterType.AUDIO_ONLY) "Bitrate:" else "Quality:",
                        style = MaterialTheme.typography.bodySmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.outline
                    )
                    availableResolutions.forEach { res ->
                        FilterChip(
                            selected = selectedResolution == res,
                            onClick = { selectedResolution = res },
                            label = { Text(res) },
                            colors = FilterChipDefaults.filterChipColors(
                                selectedContainerColor = mintGreen,
                                selectedLabelColor = Color.White
                            ),
                            shape = RoundedCornerShape(14.dp)
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // Single Download Section (After Filters: Exactly 1 Button to Download)
            if (activeOption != null) {
                val option = activeOption
                val isOptionSuccess = downloadedOptionUrl == option.url
                val isCurrentDownloading = isDownloading
                val targetBtnColor = if (isOptionSuccess) Color(0xFF2EB886) else logoRed
                val animatedBtnColor by animateColorAsState(
                    targetValue = targetBtnColor,
                    animationSpec = tween(durationMillis = 400),
                    label = "singleBtnColorAnim"
                )

                // Option Details Card
                ElevatedCard(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.elevatedCardColors(
                        containerColor = MaterialTheme.colorScheme.surface
                    )
                ) {
                    Column(modifier = Modifier.padding(14.dp)) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(
                                    text = option.resolution,
                                    style = MaterialTheme.typography.titleLarge,
                                    fontWeight = FontWeight.Bold
                                )
                                Spacer(Modifier.width(8.dp))
                                Surface(
                                    color = if (option.format == "MP4" || option.format == "M4A") logoRed.copy(alpha = 0.15f) else mintGreen.copy(alpha = 0.15f),
                                    shape = RoundedCornerShape(6.dp)
                                ) {
                                    Text(
                                        text = option.format,
                                        style = MaterialTheme.typography.labelMedium,
                                        fontWeight = FontWeight.Bold,
                                        color = if (option.format == "MP4" || option.format == "M4A") logoRed else mintGreen,
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)
                                    )
                                }
                            }
                        }
                        Spacer(Modifier.height(4.dp))
                        Text(
                            text = when {
                                option.isAudioOnly -> "Audio Track (${option.resolution} • ${option.format})"
                                option.isVideoOnly -> "Video Stream Only (No Audio)"
                                else -> "Complete Full Video (Audio + Video Muxed)"
                            },
                            style = MaterialTheme.typography.bodySmall,
                            color = mintGreen,
                            fontWeight = FontWeight.Medium
                        )
                    }
                }

                Spacer(modifier = Modifier.height(14.dp))

                // The 1 and Only Download Button (With Animation on Press / Downloading / Completed)
                Button(
                    onClick = {
                        if (isDownloading) return@Button
                        isDownloading = true
                        downloadProgress = 0
                        downloadInfo = null
                        downloadedOptionUrl = null
                        val actionLabel = if (enableSlice) "slice of ${option.resolution} (${option.format})" else "${option.resolution} (${option.format})"
                        statusMessage = "Downloading $actionLabel..."

                        downloadJob = coroutineScope.launch {
                            val success = FileDownloader.download(
                                context = context,
                                fileUrl = option.url,
                                audioUrl = option.audioUrl,
                                rawTitle = videoTitle,
                                formatExtension = option.format.lowercase(),
                                isSliceEnabled = enableSlice,
                                sliceStartSec = sliceRange.start.toInt(),
                                sliceEndSec = sliceRange.endInclusive.toInt(),
                                onProgress = { progress ->
                                    downloadProgress = progress
                                },
                                onProgressInfo = { info ->
                                    downloadProgress = info.percentage
                                    downloadInfo = info
                                }
                            )
                            isDownloading = false
                            if (success) {
                                downloadedOptionUrl = option.url
                                statusMessage = if (enableSlice) "Slice download completed!" else "Download completed!"
                                delay(4000)
                                if (downloadedOptionUrl == option.url) {
                                    downloadedOptionUrl = null
                                }
                            } else {
                                if (statusMessage != "Download cancelled") {
                                    statusMessage = "Download failed."
                                }
                            }
                        }
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(58.dp),
                    shape = RoundedCornerShape(14.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = animatedBtnColor,
                        contentColor = Color.White
                    ),
                    enabled = !isDownloading || isOptionSuccess
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.Center
                    ) {
                        // Animation graphic inside button: looping download animation while downloading, checkmark when complete!
                        AnimatedDownloadButtonGraphic(
                            modifier = Modifier
                                .size(36.dp)
                                .clip(RoundedCornerShape(8.dp)),
                            isDownloading = isCurrentDownloading,
                            isSuccess = isOptionSuccess
                        )
                        Spacer(modifier = Modifier.width(10.dp))
                        AnimatedContent(
                            targetState = when {
                                isOptionSuccess -> "success"
                                isCurrentDownloading -> "downloading"
                                else -> "idle"
                            },
                            transitionSpec = {
                                (slideInVertically { height -> height } + fadeIn())
                                    .togetherWith(slideOutVertically { height -> -height } + fadeOut())
                            },
                            label = "singleBtnMorph"
                        ) { state ->
                            when (state) {
                                "success" -> Text(
                                    text = "✓ Download Complete",
                                    fontSize = 16.sp,
                                    fontWeight = FontWeight.Bold,
                                    color = Color.White
                                )
                                "downloading" -> Text(
                                    text = "Downloading $downloadProgress%...",
                                    fontSize = 16.sp,
                                    fontWeight = FontWeight.Bold,
                                    color = Color.White
                                )
                                else -> Text(
                                    text = if (enableSlice) "Download Slice (${option.resolution} ${option.format})" else "Download ${option.resolution} ${option.format}",
                                    fontSize = 16.sp,
                                    fontWeight = FontWeight.Bold,
                                    color = Color.White
                                )
                            }
                        }
                    }
                }

                // Sleek, Uncluttered Download Progress Card (Speed, Total/Downloaded bytes, ETA, Cancel Button)
                if (isDownloading) {
                    val info = downloadInfo
                    Spacer(modifier = Modifier.height(14.dp))
                    ElevatedCard(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(14.dp),
                        colors = CardDefaults.elevatedCardColors(
                            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
                        )
                    ) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            // Header: Stage + Percentage Pill
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = when {
                                        info?.isMuxing == true -> "Muxing Media Tracks..."
                                        info?.isTrimming == true -> "Trimming Video Clip..."
                                        info?.stage?.isNotEmpty() == true -> info.stage
                                        else -> "Downloading Media..."
                                    },
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontWeight = FontWeight.Bold
                                )
                                Surface(
                                    color = mintGreen.copy(alpha = 0.15f),
                                    shape = RoundedCornerShape(8.dp)
                                ) {
                                    Text(
                                        text = "${info?.percentage ?: downloadProgress}%",
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.Bold,
                                        color = mintGreen,
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)
                                    )
                                }
                            }

                            Spacer(modifier = Modifier.height(10.dp))

                            // Smooth Linear Progress Bar
                            LinearProgressIndicator(
                                progress = { (info?.percentage ?: downloadProgress) / 100f },
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(8.dp)
                                    .clip(RoundedCornerShape(4.dp)),
                                color = mintGreen,
                                trackColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f)
                            )

                            Spacer(modifier = Modifier.height(12.dp))

                            // 3-Column Clean Metrics: Downloaded/Total, Speed, ETA
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                // Metric 1: Downloaded / Total
                                Column {
                                    Text(
                                        text = "DOWNLOADED",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.outline,
                                        fontWeight = FontWeight.SemiBold
                                    )
                                    Spacer(modifier = Modifier.height(2.dp))
                                    Text(
                                        text = if (info != null && info.totalBytes > 0) "${info.downloadedFormatted} / ${info.totalFormatted}" else "${info?.downloadedFormatted ?: "0 B"}",
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.Bold
                                    )
                                }

                                // Metric 2: Speed
                                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                    Text(
                                        text = "SPEED",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.outline,
                                        fontWeight = FontWeight.SemiBold
                                    )
                                    Spacer(modifier = Modifier.height(2.dp))
                                    Text(
                                        text = info?.speedFormatted ?: "--/s",
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.Bold,
                                        color = mintGreen
                                    )
                                }

                                // Metric 3: ETA
                                Column(horizontalAlignment = Alignment.End) {
                                    Text(
                                        text = "TIME LEFT",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.outline,
                                        fontWeight = FontWeight.SemiBold
                                    )
                                    Spacer(modifier = Modifier.height(2.dp))
                                    Text(
                                        text = info?.etaFormatted ?: "--",
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.Bold,
                                        color = logoRed
                                    )
                                }
                            }

                            Spacer(modifier = Modifier.height(12.dp))

                            // Inline Cancel Download Button
                            OutlinedButton(
                                onClick = {
                                    downloadJob?.cancel()
                                    downloadJob = null
                                    isDownloading = false
                                    downloadProgress = 0
                                    downloadInfo = null
                                    DownloadNotificationHelper.cancel(context)
                                    statusMessage = "Download cancelled"
                                },
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(42.dp),
                                shape = RoundedCornerShape(10.dp),
                                colors = ButtonDefaults.outlinedButtonColors(
                                    contentColor = logoRed
                                ),
                                border = BorderStroke(1.dp, logoRed.copy(alpha = 0.6f))
                            ) {
                                Text(
                                    text = "Cancel Download",
                                    fontWeight = FontWeight.Bold
                                )
                            }
                        }
                    }
                }
            } else if (streamOptions.isNotEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "No stream found for $selectedFormatFilter. Please choose another format.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.outline
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(30.dp))

        // Exit Button at Bottom
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Center
        ) {
            TextButton(
                onClick = { (context as? Activity)?.finishAffinity() }
            ) {
                Text(
                    text = "Exit StreamFetch",
                    color = MaterialTheme.colorScheme.outline,
                    fontWeight = FontWeight.SemiBold
                )
            }
        }
    }
}
