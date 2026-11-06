# StreamFetch

Fast, lightweight desktop media grabber with a DirectX 11 / Dear ImGui interface.

Wraps `yt-dlp` and `ffmpeg` to fetch and mux high-quality video and audio streams from supported web endpoints.

## Features

- **Format Detection:** Inspects available resolutions from 144p up to 4K.
- **Modes:** Video with Audio, Video Only, Audio Only (MP3 extraction).
- **Time Slicing:** Download specific segments without fetching the full video.
- **Fast Downloader:** Multi-fragment concurrency with HTTP chunk streaming.
- **DirectX 11 GUI:** Responsive native UI rendered via Dear ImGui.

## Prerequisites

Ensure these dependencies exist in your `PATH` or the executable working directory:

1. [yt-dlp](https://github.com/yt-dlp/yt-dlp)
2. [ffmpeg](https://ffmpeg.org/download.html)
3. Node.js runtime (for cipher challenge solving)

## Build

Compile using MinGW (w64devkit / GCC 16+) or MSVC with DirectX 11 dependencies:

```powershell
# MinGW (GCC) build command
g++ -O2 -std=c++17 -Iimgui gui_imgui.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_impl_win32.cpp imgui/imgui_impl_dx11.cpp -o StreamFetch.exe -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lole32 -lshell32 -luuid -mwindows

# CLI version
g++ -O2 -std=c++17 main.cpp -o StreamFetch_cli.exe
```

## License

MIT License. Educational and personal archival purposes only.
