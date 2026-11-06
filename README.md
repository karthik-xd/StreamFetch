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

Compile using MSVC (`cl.exe`) or MinGW with DirectX 11 dependencies:

```powershell
# Windows MSVC example
cl.exe /O2 /MD /EHsc gui_imgui.cpp imgui/*.cpp /Iimgui /Fe:StreamFetch.exe /link d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib ole32.lib shell32.lib
```

## License

MIT License. Educational and personal archival purposes only.
