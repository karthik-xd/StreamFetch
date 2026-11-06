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

## Legal Disclaimer & Compliance

This software is developed strictly for educational, research, and personal archival purposes.

- **Content Rights:** Users are solely responsible for ensuring they have the legal right to access and download media. Only download media you own, public domain works, or content under open licenses (e.g., Creative Commons).
- **Platform Terms:** Automated media retrieval may violate platform terms of service. Authors assume no liability for misuse, account restrictions, or copyright infringement.
- **No DRM Circumvention:** StreamFetch does not decrypt, bypass, or circumvent DRM protection technologies (e.g., Widevine).
- **Trademark Notice:** StreamFetch is an independent project and is not affiliated with, authorized, maintained, or endorsed by YouTube or Google LLC.

## License

Released under the [MIT License](LICENSE). Copyright (c) 2026 karthik-xd.
