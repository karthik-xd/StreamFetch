package com.streamfetch.app.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Logo Colors: Mint Cloud (#47D1A2) + YouTube Red (#E52E2E) + Dark Charcoal (#1F2421)
val MintPrimary = Color(0xFF2EB886)
val MintLight = Color(0xFFE2F9F0)
val MintDark = Color(0xFF1E8A63)

val LogoRed = Color(0xFFE52E2E)
val LogoRedContainer = Color(0xFFFFDAD6)
val OnLogoRed = Color(0xFFFFFFFF)

val DarkBackground = Color(0xFF121815)
val DarkSurface = Color(0xFF1B2320)
val DarkSurfaceVariant = Color(0xFF26322D)

val LightBackground = Color(0xFFF6FBF8)
val LightSurface = Color(0xFFFFFFFF)
val LightSurfaceVariant = Color(0xFFE5EFEA)

private val LightColorScheme = lightColorScheme(
    primary = MintPrimary,
    onPrimary = Color.White,
    primaryContainer = MintLight,
    onPrimaryContainer = MintDark,
    secondary = LogoRed,
    onSecondary = OnLogoRed,
    secondaryContainer = LogoRedContainer,
    onSecondaryContainer = Color(0xFF410002),
    background = LightBackground,
    surface = LightSurface,
    surfaceVariant = LightSurfaceVariant,
    onBackground = Color(0xFF18201C),
    onSurface = Color(0xFF18201C)
)

private val DarkColorScheme = darkColorScheme(
    primary = MintPrimary,
    onPrimary = Color.Black,
    primaryContainer = MintDark,
    onPrimaryContainer = MintLight,
    secondary = LogoRed,
    onSecondary = OnLogoRed,
    secondaryContainer = Color(0xFF93000A),
    onSecondaryContainer = LogoRedContainer,
    background = DarkBackground,
    surface = DarkSurface,
    surfaceVariant = DarkSurfaceVariant,
    onBackground = Color(0xFFE1E8E4),
    onSurface = Color(0xFFE1E8E4)
)

@Composable
fun StreamFetchTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme
    MaterialTheme(
        colorScheme = colorScheme,
        content = content
    )
}
