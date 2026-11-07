# StreamFetch

Fast, lightweight multi-platform media grabber wrapping modern extraction engines and `ffmpeg` to fetch and mux high-quality video and audio streams.

---

## Repository Structure

```text
StreamFetch/
├── Windows/                        # Windows Native Desktop App (DirectX 11 / Dear ImGui)
│   ├── StreamFetch.exe             # Precompiled 64-bit GUI application
│   ├── StreamFetch_cli.exe         # Precompiled 64-bit CLI application
│   ├── gui_imgui.cpp               # DirectX 11 / ImGui GUI source
│   ├── main.cpp                    # CLI source
│   └── imgui/                      # Dear ImGui library sources
│
├── android/                        # Android Edition (Kotlin / Jetpack Compose)
│   ├── download apk/               # Precompiled Android application package
│   │   └── StreamFetch.apk         # Ready-to-install Android APK (~7.4 MB)
│   ├── app/                        # Android application source & resources
│   ├── animation/                  # Animated download button graphic & video loop
│   ├── gradle/                     # Gradle wrapper
│   └── README.md                   # Android build and setup guide
│
├── LICENSE                         # MIT License
└── README.md                       # Project overview
```

---

## 💻 Windows Desktop

- **GUI Binary**: [`Windows/StreamFetch.exe`](Windows/StreamFetch.exe)
- **CLI Binary**: [`Windows/StreamFetch_cli.exe`](Windows/StreamFetch_cli.exe)
- **Framework**: DirectX 11 native rendering with Dear ImGui.
- **Features**: Format inspection (144p to 4K), audio extraction (MP3), time slicing, multi-fragment concurrency.

### Build (MinGW / MSVC)
```powershell
cd Windows

# MinGW (GCC) GUI
g++ -O2 -std=c++17 -Iimgui gui_imgui.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_impl_win32.cpp imgui/imgui_impl_dx11.cpp -o StreamFetch.exe -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lole32 -lshell32 -luuid -mwindows

# CLI version
g++ -O2 -std=c++17 main.cpp -o StreamFetch_cli.exe
```

---

## 📱 Android Edition

- **Pre-built APK**: [`android/download apk/StreamFetch.apk`](android/download%20apk/StreamFetch.apk)
- **Framework**: 100% Native Kotlin + Jetpack Compose.
- **Features**:
  - Zero Python dependency (~7.4 MB APK size).
  - Share-sheet intent receiver (share directly from YouTube app).
  - Custom storage picker via Android Storage Access Framework (`OpenDocumentTree`).
  - Dynamic looping download animation with real-time progress indicators.

### Build
```bash
cd android
./gradlew assembleDebug
# Generated APK: android/app/build/outputs/apk/debug/app-debug.apk
```

---

## Legal Disclaimer & Compliance

This software is developed strictly for educational, research, and personal archival purposes.

- **Content Rights:** Users are solely responsible for ensuring they have the legal right to access and download media. Only download media you own, public domain works, or content under open licenses (e.g., Creative Commons).
- **Platform Terms:** Automated media retrieval may violate platform terms of service. Authors assume no liability for misuse, account restrictions, or copyright infringement.
- **No DRM Circumvention:** StreamFetch does not decrypt, bypass, or circumvent DRM protection technologies.
- **Trademark Notice:** StreamFetch is an independent project and is not affiliated with, authorized, maintained, or endorsed by YouTube or Google LLC.

---

## License

Released under the [MIT License](LICENSE). Copyright (c) 2026 karthik-xd.
