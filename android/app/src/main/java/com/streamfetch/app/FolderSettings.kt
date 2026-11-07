package com.streamfetch.app

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
fun DownloadLocationSettings() {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences("streamfetch_prefs", Context.MODE_PRIVATE) }

    var selectedFolderUri by remember {
        mutableStateOf(prefs.getString("custom_folder_uri", null))
    }

    val folderPickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        if (uri != null) {
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            context.contentResolver.takePersistableUriPermission(uri, flags)
            prefs.edit().putString("custom_folder_uri", uri.toString()).apply()
            selectedFolderUri = uri.toString()
        }
    }

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "Storage Destination",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                text = if (selectedFolderUri == null) "Default: Public Downloads" else "Custom: ${Uri.parse(selectedFolderUri).lastPathSegment}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(10.dp))
            Row {
                Button(
                    onClick = { folderPickerLauncher.launch(null) },
                    shape = RoundedCornerShape(10.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Text("Change Folder")
                }
                if (selectedFolderUri != null) {
                    Spacer(modifier = Modifier.width(8.dp))
                    OutlinedButton(
                        onClick = {
                            prefs.edit().remove("custom_folder_uri").apply()
                            selectedFolderUri = null
                        },
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text("Reset Default")
                    }
                }
            }
        }
    }
}
