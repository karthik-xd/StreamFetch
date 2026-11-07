#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// DirectX 11 State
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static HWND                     g_hWnd = nullptr;
static ImFont*                   g_fontRegular = nullptr;
static ImFont*                   g_fontTitle = nullptr;
static ImFont*                   g_fontBold = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Data Structures
struct QualityOption {
    int height = 0;
    std::string size_str;
    std::string display_label;
};

struct DownloadProgress {
    double percent = 0.0;
    std::string downloaded_str;
    std::string total_str;
    std::string left_str;
    std::string speed_str;
    std::string eta_str;
};

struct AppState {
    char url_input[1024] = "";
    char folder_input[1024] = "";
    std::string video_title = "Paste a YouTube URL and click Fetch Info";
    int video_duration = 0;

    int download_mode = 0; // 0 = Video + Audio, 1 = Video Only, 2 = Audio Only (MP3)
    int selected_quality_idx = 0;
    std::vector<QualityOption> qualities;
    std::string audio_size_str;

    int selected_video_format_idx = 0; // 0 = MP4, 1 = MKV, 2 = WEBM, 3 = MOV, 4 = AVI
    int selected_audio_format_idx = 0; // 0 = MP3, 1 = M4A, 2 = WAV, 3 = FLAC, 4 = OPUS

    bool enable_slice = false;
    int slice_start = 0;
    int slice_end = 60;
    int dragging_thumb = 0; // 0: none, 1: start, 2: end

    std::atomic<bool> is_busy{false};
    std::atomic<bool> is_downloading{false};
    std::string status_message = "Ready.";

    DownloadProgress progress;
    std::mutex progress_mutex;

    std::vector<std::string> log_lines;
    std::mutex log_mutex;
    bool scroll_to_bottom = false;
};

AppState g_app;

// Helper Functions
std::string FormatTime(int total_sec) {
    if (total_sec < 0) total_sec = 0;
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;
    char buf[64];
    if (h > 0) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }
    return std::string(buf);
}

double ParseSizeToMB(const std::string& s) {
    std::regex r(R"((\d+(?:\.\d+)?)\s*(KiB|MiB|GiB|kB|MB|GB|B)?)", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(s, m, r)) {
        double val = std::stod(m[1].str());
        std::string unit = m[2].str();
        for (auto& c : unit) c = (char)tolower(c);
        if (unit == "gib" || unit == "gb") return val * 1024.0;
        if (unit == "kib" || unit == "kb") return val / 1024.0;
        if (unit == "b") return val / (1024.0 * 1024.0);
        return val;
    }
    return 0.0;
}

std::string FormatMB(double mb) {
    char buf[64];
    if (mb >= 1024.0) {
        snprintf(buf, sizeof(buf), "%.2f GiB", mb / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%.2f MiB", mb);
    }
    return std::string(buf);
}

void AddLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_app.log_mutex);
    if (g_app.log_lines.size() > 500) {
        g_app.log_lines.erase(g_app.log_lines.begin(), g_app.log_lines.begin() + 100);
    }
    g_app.log_lines.push_back(line);
    g_app.scroll_to_bottom = true;
}

// Open Native Windows Folder Browser Dialog (With "New Folder" button)
std::string OpenFolderPicker(HWND parentHwnd) {
    std::string resultPath = "";
    IFileDialog* pfd = nullptr;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        pfd->SetTitle(L"Choose Download Folder (or Create New Folder)");

        if (SUCCEEDED(pfd->Show(parentHwnd))) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
                    if (len > 0) {
                        std::string utf8Str(len - 1, 0);
                        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &utf8Str[0], len, NULL, NULL);
                        resultPath = utf8Str;
                    }
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    return resultPath;
}

// Background Worker: Fetch Formats & Duration
void WorkerFetchMetadata(std::string url) {
    g_app.status_message = "Connecting to YouTube and querying format table...";
    AddLog("[info] Querying metadata for " + url);

    std::string cmd = "yt-dlp --print \"%(title)s\" --print \"%(duration)s\" -F --no-playlist \"" + url + "\"";
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        g_app.status_message = "Error: Failed to launch yt-dlp.";
        g_app.is_busy = false;
        return;
    }

    char buffer[1024];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    _pclose(pipe);

    std::istringstream stream(output);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }

    if (lines.size() < 2) {
        g_app.status_message = "Error: Could not retrieve video info.";
        g_app.is_busy = false;
        return;
    }

    std::string durStr = lines.back();
    std::string titleStr = lines[lines.size() - 2];

    g_app.video_title = titleStr;
    int dur = 0;
    try { dur = std::stoi(durStr); } catch (...) { dur = 60; }
    if (dur <= 0) dur = 60;
    g_app.video_duration = dur;
    g_app.slice_start = 0;
    g_app.slice_end = dur;

    // Parse formats and file sizes
    std::map<int, QualityOption> qualityMap;
    std::regex vidRegex(R"((\d{3,4})x(\d{3,4})|\b(\d{3,4})p(?:60)?)", std::regex_constants::icase);
    std::regex sizeRegex(R"(\|\s*~?\s*(\d+(?:\.\d+)?\s*(?:KiB|MiB|GiB|kB|MB|GB|B)))", std::regex_constants::icase);

    std::string bestAudio = "";

    for (const auto& l : lines) {
        if (l.find("audio only") != std::string::npos) {
            std::smatch sm;
            if (std::regex_search(l, sm, sizeRegex)) {
                bestAudio = sm[1].str();
            }
        }

        if (l.find("video only") != std::string::npos || l.find("avc1") != std::string::npos || l.find("vp9") != std::string::npos) {
            std::smatch vm;
            if (std::regex_search(l, vm, vidRegex)) {
                int h = 0;
                if (vm[2].matched) h = std::stoi(vm[2].str());
                else if (vm[3].matched) h = std::stoi(vm[3].str());
                else if (vm[1].matched) h = std::stoi(vm[1].str());

                if (h >= 144) {
                    std::string sz = "size unlisted";
                    std::smatch sm;
                    if (std::regex_search(l, sm, sizeRegex)) {
                        sz = sm[1].str();
                    }

                    QualityOption opt;
                    opt.height = h;
                    opt.size_str = sz;
                    qualityMap[h] = opt;
                }
            }
        }
    }

    g_app.audio_size_str = bestAudio;
    g_app.qualities.clear();

    for (auto it = qualityMap.rbegin(); it != qualityMap.rend(); ++it) {
        g_app.qualities.push_back(it->second);
    }

    if (!g_app.qualities.empty()) {
        int max_h = g_app.qualities.front().height;
        int min_h = g_app.qualities.back().height;

        for (auto& q : g_app.qualities) {
            std::string label = std::to_string(q.height) + "p";
            if (q.height == max_h) label += " (Max)";
            if (q.height == min_h) label += " (Min)";
            if (!q.size_str.empty() && q.size_str != "size unlisted") {
                label += " ~ " + q.size_str;
            }
            q.display_label = label;
        }

        g_app.status_message = "Loaded " + std::to_string(g_app.qualities.size()) + " formats. Ready.";
        AddLog("[success] Discovered " + std::to_string(g_app.qualities.size()) + " stream formats.");
    } else {
        g_app.status_message = "Video loaded. Default stream ready.";
    }

    g_app.selected_quality_idx = 0;
    g_app.is_busy = false;
}

// Background Worker: High-Performance Continuous Download
void WorkerDownload(std::string cmd, std::string targetFolder) {
    g_app.is_downloading = true;
    g_app.status_message = "Downloading...";
    AddLog("[folder] Target Directory: " + targetFolder);
    AddLog("[download] Command: " + cmd);

    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        g_app.status_message = "Error starting download process.";
        g_app.is_downloading = false;
        g_app.is_busy = false;
        return;
    }

    char buffer[1024];
    std::regex progRegex(R"(\[download\]\s+(\d+(?:\.\d+)?)%\s+of\s+~?\s*(\d+(?:\.\d+)?\s*(?:KiB|MiB|GiB|kB|MB|GB|B))\s+at\s+(\S+)\s+ETA\s+(\S+))", std::regex_constants::icase);

    auto last_update = std::chrono::steady_clock::now();

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line = buffer;
        std::smatch m;
        if (std::regex_search(line, m, progRegex)) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() >= 120) {
                last_update = now;

                DownloadProgress dp;
                dp.percent = std::stod(m[1].str());
                dp.total_str = m[2].str();
                dp.speed_str = m[3].str();
                dp.eta_str = m[4].str();

                double totalMB = ParseSizeToMB(m[2].str());
                double downloadedMB = totalMB * (dp.percent / 100.0);
                double leftMB = std::max(0.0, totalMB - downloadedMB);

                dp.downloaded_str = FormatMB(downloadedMB);
                dp.left_str = FormatMB(leftMB);

                {
                    std::lock_guard<std::mutex> lock(g_app.progress_mutex);
                    g_app.progress = dp;
                }
            }
        } else {
            if (line.find("[download]") == std::string::npos && line.find("\r") == std::string::npos && line.size() > 2) {
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                AddLog(line);
            }
        }
    }

    int code = _pclose(pipe);
    g_app.is_downloading = false;
    g_app.is_busy = false;

    if (code == 0) {
        g_app.progress.percent = 100.0;
        g_app.status_message = "Download Complete (Saved to folder)!";
        AddLog("[success] File successfully saved to: " + targetFolder);
    } else {
        g_app.status_message = "Download Failed. Check output log.";
        AddLog("[error] Download exited with error code " + std::to_string(code));
    }
}

// Apply "Electric Social" Palette to Dear ImGui
void SetupElectricSocialStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                  = ImVec4(0.945f, 0.961f, 0.976f, 1.00f); // #F1F5F9 Soft White
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.580f, 0.639f, 0.722f, 1.00f); // #94A3B8 Muted Text
    colors[ImGuiCol_WindowBg]              = ImVec4(0.031f, 0.059f, 0.125f, 1.00f); // #080F20 Deep Navy
    colors[ImGuiCol_ChildBg]               = ImVec4(0.067f, 0.110f, 0.196f, 1.00f); // #111C32 Surface
    colors[ImGuiCol_PopupBg]               = ImVec4(0.067f, 0.110f, 0.196f, 0.98f); // #111C32
    colors[ImGuiCol_Border]                = ImVec4(0.140f, 0.205f, 0.325f, 1.00f); // Crisp visible card border
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.045f, 0.080f, 0.150f, 1.00f); // #0B1426 Darker inputs
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.085f, 0.140f, 0.240f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.120f, 0.190f, 0.320f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.031f, 0.059f, 0.125f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.067f, 0.110f, 0.196f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.031f, 0.059f, 0.125f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.067f, 0.110f, 0.196f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.031f, 0.059f, 0.125f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.145f, 0.220f, 0.365f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.231f, 0.510f, 0.965f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.133f, 0.827f, 0.933f, 1.00f); // #22D3EE Neon Cyan
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.231f, 0.510f, 0.965f, 1.00f); // #3B82F6 Electric Blue
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.133f, 0.827f, 0.933f, 1.00f); // #22D3EE
    colors[ImGuiCol_Button]                = ImVec4(0.231f, 0.510f, 0.965f, 1.00f); // #3B82F6
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.145f, 0.388f, 0.922f, 1.00f); // #2563EB
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.114f, 0.306f, 0.843f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.145f, 0.220f, 0.365f, 0.80f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.231f, 0.510f, 0.965f, 0.80f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.140f, 0.205f, 0.325f, 1.00f); // #1E293B
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.231f, 0.510f, 0.965f, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.145f, 0.220f, 0.365f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.231f, 0.510f, 0.965f, 0.78f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);
    colors[ImGuiCol_PlotLines]             = ImVec4(0.133f, 0.827f, 0.933f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = ImVec4(0.133f, 0.827f, 0.933f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.231f, 0.510f, 0.965f, 0.35f);

    style.WindowPadding    = ImVec2(18.0f, 18.0f);
    style.FramePadding     = ImVec2(12.0f, 8.0f);
    style.ItemSpacing      = ImVec2(12.0f, 11.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowRounding   = 12.0f;
    style.ChildRounding    = 10.0f;
    style.FrameRounding    = 8.0f;
    style.PopupRounding    = 8.0f;
    style.ScrollbarRounding= 6.0f;
    style.GrabRounding     = 6.0f;
    style.TabRounding      = 8.0f;
}

// Render Interactive Dual-Thumb Seekbar / Range Slider
void RenderDualRangeSlider(float width, float height) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool mouseDown = ImGui::GetIO().MouseDown[0];

    int dur = (g_app.video_duration > 0) ? g_app.video_duration : 60;
    float margin = 14.0f;
    float trackWidth = width - margin * 2.0f;
    float trackY = pos.y + 16.0f;
    float trackH = 6.0f;

    ImGui::InvisibleButton("##DualSliderHitBox", ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();

    float xStart = pos.x + margin + ((float)g_app.slice_start / dur) * trackWidth;
    float xEnd   = pos.x + margin + ((float)g_app.slice_end   / dur) * trackWidth;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float distStart = std::abs(mouse.x - xStart);
        float distEnd   = std::abs(mouse.x - xEnd);
        g_app.dragging_thumb = (distStart <= distEnd) ? 1 : 2;
    }

    if (g_app.dragging_thumb > 0) {
        if (mouseDown) {
            float clampedX = std::max(pos.x + margin, std::min(pos.x + margin + trackWidth, mouse.x));
            float ratio = (clampedX - (pos.x + margin)) / trackWidth;
            int targetSec = (int)(ratio * dur);

            if (g_app.dragging_thumb == 1) {
                g_app.slice_start = std::max(0, std::min(targetSec, g_app.slice_end - 1));
            } else if (g_app.dragging_thumb == 2) {
                g_app.slice_end = std::min(dur, std::max(targetSec, g_app.slice_start + 1));
            }
        } else {
            g_app.dragging_thumb = 0;
        }
    }

    xStart = pos.x + margin + ((float)g_app.slice_start / dur) * trackWidth;
    xEnd   = pos.x + margin + ((float)g_app.slice_end   / dur) * trackWidth;

    // 1. Base Track (#080F20)
    drawList->AddRectFilled(
        ImVec2(pos.x + margin, trackY - trackH * 0.5f),
        ImVec2(pos.x + margin + trackWidth, trackY + trackH * 0.5f),
        IM_COL32(8, 15, 32, 255), 3.0f
    );

    // 2. Active Range Bar (Neon Cyan: #22D3EE)
    drawList->AddRectFilled(
        ImVec2(xStart, trackY - trackH * 0.5f),
        ImVec2(xEnd, trackY + trackH * 0.5f),
        IM_COL32(34, 211, 238, 255), 3.0f
    );

    // 3. Start Thumb (Electric Blue: #3B82F6)
    float r = 9.0f;
    drawList->AddCircleFilled(ImVec2(xStart, trackY), r, IM_COL32(59, 130, 246, 255));
    drawList->AddCircle(ImVec2(xStart, trackY), r, IM_COL32(241, 245, 249, 255), 16, 2.0f);
    drawList->AddCircleFilled(ImVec2(xStart, trackY), 2.5f, IM_COL32(255, 255, 255, 255));

    // 4. End Thumb (Violet: #8B5CF6)
    drawList->AddCircleFilled(ImVec2(xEnd, trackY), r, IM_COL32(139, 92, 246, 255));
    drawList->AddCircle(ImVec2(xEnd, trackY), r, IM_COL32(241, 245, 249, 255), 16, 2.0f);
    drawList->AddCircleFilled(ImVec2(xEnd, trackY), 2.5f, IM_COL32(255, 255, 255, 255));

    // 5. Timestamps
    float textY = trackY + 14.0f;
    std::string strStart = "Start: " + FormatTime(g_app.slice_start);
    drawList->AddText(ImVec2(pos.x + margin, textY), IM_COL32(34, 211, 238, 255), strStart.c_str());

    std::string strSlice = "Slice: " + FormatTime(g_app.slice_end - g_app.slice_start);
    ImVec2 szSlice = ImGui::CalcTextSize(strSlice.c_str());
    drawList->AddText(ImVec2(pos.x + (width - szSlice.x) * 0.5f, textY), IM_COL32(241, 245, 249, 255), strSlice.c_str());

    std::string strEnd = "End: " + FormatTime(g_app.slice_end) + " / " + FormatTime(dur);
    ImVec2 szEnd = ImGui::CalcTextSize(strEnd.c_str());
    drawList->AddText(ImVec2(pos.x + margin + trackWidth - szEnd.x, textY), IM_COL32(139, 92, 246, 255), strEnd.c_str());
}

// Render Main ImGui Window
void RenderUI() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("MainContainer", nullptr, flags);

    // Header Title
    if (g_fontTitle) ImGui::PushFont(g_fontTitle);
    ImGui::TextColored(ImVec4(0.945f, 0.961f, 0.976f, 1.0f), "StreamFetch");
    if (g_fontTitle) ImGui::PopFont();

    ImGui::TextColored(ImVec4(0.580f, 0.639f, 0.722f, 1.0f), "High-speed multi-threaded media downloader with lossless range slicing");
    ImGui::Spacing();

    ImGuiWindowFlags cardFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    // URL Card
    ImGui::BeginChild("UrlCard", ImVec2(0, 118), true, cardFlags);
    ImGui::TextColored(ImVec4(0.945f, 0.961f, 0.976f, 1.0f), "Video URL");

    float btnWidth = 115.0f;
    float inputWidth = ImGui::GetContentRegionAvail().x - btnWidth - 10.0f;
    ImGui::SetNextItemWidth(inputWidth);
    ImGui::InputText("##UrlInput", g_app.url_input, sizeof(g_app.url_input));

    ImGui::SameLine();
    bool disabled = g_app.is_busy || g_app.is_downloading;
    if (disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Fetch Info", ImVec2(btnWidth, 0))) {
        if (strlen(g_app.url_input) > 0) {
            g_app.is_busy = true;
            std::thread(WorkerFetchMetadata, std::string(g_app.url_input)).detach();
        }
    }
    if (disabled) ImGui::EndDisabled();

    ImGui::TextColored(ImVec4(0.580f, 0.639f, 0.722f, 1.0f), "Title: %s (%s)",
                       g_app.video_title.c_str(), FormatTime(g_app.video_duration).c_str());
    ImGui::EndChild();

    // Save Location Card (User Can Pick Folder / Create New Folder)
    ImGui::BeginChild("SaveLocationCard", ImVec2(0, 125), true, cardFlags);
    ImGui::TextColored(ImVec4(0.945f, 0.961f, 0.976f, 1.0f), "Save Destination (Folder / New Folder)");

    float browseBtnWidth = 115.0f;
    float folderInputWidth = ImGui::GetContentRegionAvail().x - browseBtnWidth - 10.0f;
    ImGui::SetNextItemWidth(folderInputWidth);
    ImGui::InputText("##FolderInput", g_app.folder_input, sizeof(g_app.folder_input));

    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(browseBtnWidth, 0))) {
        std::string chosen = OpenFolderPicker(g_hWnd);
        if (!chosen.empty()) {
            strncpy(g_app.folder_input, chosen.c_str(), sizeof(g_app.folder_input) - 1);
            AddLog("[folder] Selected folder: " + chosen);
        }
    }
    ImGui::TextColored(ImVec4(0.580f, 0.639f, 0.722f, 1.0f), "Files will be saved inside this directory (created automatically if new).");
    ImGui::EndChild();

    // Mode & Quality & Format Grid
    ImGui::BeginChild("SettingsCard", ImVec2(0, 145), true, cardFlags);
    ImGui::Columns(3, "SettingsColumns", false);
    ImGui::SetColumnWidth(0, 210.0f);
    ImGui::SetColumnWidth(1, 280.0f);

    // Left Column: Download Mode
    ImGui::TextColored(ImVec4(0.945f, 0.961f, 0.976f, 1.0f), "Download Mode");
    ImGui::Spacing();
    ImGui::RadioButton("Video + Audio", &g_app.download_mode, 0);
    ImGui::RadioButton("Video Only", &g_app.download_mode, 1);
    ImGui::RadioButton("Audio Only", &g_app.download_mode, 2);

    ImGui::NextColumn();

    // Middle Column: Quality Dropdown with Sizes
    ImGui::TextColored(ImVec4(0.945f, 0.961f, 0.976f, 1.0f), "Quality & Size");
    ImGui::Spacing();

    if (g_app.download_mode == 2) {
        std::string aText = "Audio ~ " + (g_app.audio_size_str.empty() ? "Standard" : g_app.audio_size_str);
        ImGui::TextColored(ImVec4(0.133f, 0.827f, 0.933f, 1.0f), "%s", aText.c_str());
        ImGui::TextColored(ImVec4(0.580f, 0.639f, 0.722f, 1.0f), "YouTube Audio Stream");
    } else {
        if (!g_app.qualities.empty()) {
            std::string preview = g_app.qualities[g_app.selected_quality_idx].display_label;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##QualityCombo", preview.c_str())) {
                for (int i = 0; i < (int)g_app.qualities.size(); i++) {
                    bool isSelected = (g_app.selected_quality_idx == i);
                    if (ImGui::Selectable(g_app.qualities[i].display_label.c_str(), isSelected)) {
                        g_app.selected_quality_idx = i;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextColored(ImVec4(0.580f, 0.639f, 0.722f, 1.0f), "Best Available");
        }
    }

    ImGui::NextColumn();

    // Right Column: Output Format
    ImGui::TextColored(ImVec4(0.945f, 0.961f, 0.976f, 1.0f), "File Format");
    ImGui::Spacing();
    const char* videoFormats[] = { "MP4 (.mp4)", "MKV (.mkv)", "WEBM (.webm)", "MOV (.mov)", "AVI (.avi)" };
    const char* audioFormats[] = { "MP3 (.mp3)", "M4A (.m4a)", "WAV (.wav)", "FLAC (.flac)", "OPUS (.opus)" };

    if (g_app.download_mode == 2) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##AudioFormatCombo", audioFormats[g_app.selected_audio_format_idx])) {
            for (int i = 0; i < (int)IM_ARRAYSIZE(audioFormats); i++) {
                bool isSelected = (g_app.selected_audio_format_idx == i);
                if (ImGui::Selectable(audioFormats[i], isSelected)) {
                    g_app.selected_audio_format_idx = i;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##VideoFormatCombo", videoFormats[g_app.selected_video_format_idx])) {
            for (int i = 0; i < (int)IM_ARRAYSIZE(videoFormats); i++) {
                bool isSelected = (g_app.selected_video_format_idx == i);
                if (ImGui::Selectable(videoFormats[i], isSelected)) {
                    g_app.selected_video_format_idx = i;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Columns(1);
    ImGui::EndChild();

    // Dual-Point Range Slider (Seekbar)
    float sliceCardHeight = g_app.enable_slice ? 122.0f : 52.0f;
    ImGui::BeginChild("SliceCard", ImVec2(0, sliceCardHeight), true, cardFlags);
    ImGui::Checkbox("Slice Video (Custom Range Seekbar)", &g_app.enable_slice);
    if (g_app.enable_slice) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.956f, 0.447f, 0.714f, 1.0f), "• Drag Blue & Violet thumbs to set slice");
        RenderDualRangeSlider(ImGui::GetContentRegionAvail().x, 48.0f);
    }
    ImGui::EndChild();

    // Format extensions mapping
    const char* vExts[] = { "mp4", "mkv", "webm", "mov", "avi" };
    const char* aExts[] = { "mp3", "m4a", "wav", "flac", "opus" };
    std::string chosenExt = (g_app.download_mode == 2) ?
                            aExts[g_app.selected_audio_format_idx] :
                            vExts[g_app.selected_video_format_idx];

    // Download Button
    std::string btnText = g_app.enable_slice ? ("Download Video Slice (" + chosenExt + ")") :
                         (g_app.download_mode == 2 ? ("Download Audio (" + chosenExt + ")") : ("Download Video (" + chosenExt + ")"));

    if (g_app.is_downloading) btnText = "Downloading...";

    bool canDownload = !g_app.is_busy && !g_app.is_downloading && strlen(g_app.url_input) > 0;
    if (!canDownload) ImGui::BeginDisabled();

    if (g_fontBold) ImGui::PushFont(g_fontBold);
    bool clickedDownload = ImGui::Button(btnText.c_str(), ImVec2(-1, 46));
    if (g_fontBold) ImGui::PopFont();

    if (clickedDownload) {
        // Ensure folder exists or create it
        std::string targetFolder = (strlen(g_app.folder_input) > 0) ? g_app.folder_input : ".";
        try {
            std::filesystem::create_directories(targetFolder);
        } catch (...) {}

        std::string cmd = "yt-dlp --newline --no-mtime ";
        cmd += "--paths \"home:" + targetFolder + "\" ";
        cmd += "--http-chunk-size 10M ";
        cmd += "--concurrent-fragments 4 ";
        cmd += "--retries 25 --fragment-retries 25 --file-access-retries 10 ";
        cmd += "--retry-sleep fragment:exp=1:10 ";
        cmd += "--buffer-size 16M ";
        cmd += "--js-runtimes \"node:C:\\node js\\node.exe\" ";

        if (g_app.enable_slice) {
            cmd += "--download-sections \"*" + std::to_string(g_app.slice_start) + "-" +
                   std::to_string(g_app.slice_end) + "\" ";
        }

        if (g_app.download_mode == 2) {
            cmd += "-x --audio-format " + chosenExt + " -f \"bestaudio/best\" ";
        } else {
            int h = (!g_app.qualities.empty()) ? g_app.qualities[g_app.selected_quality_idx].height : 0;
            std::string hFilter = (h > 0) ? "[height<=" + std::to_string(h) + "]" : "";
            if (g_app.download_mode == 1) {
                cmd += "-f \"bestvideo" + hFilter + "\" ";
            } else {
                cmd += "-f \"bestvideo" + hFilter + "+bestaudio/best" + hFilter + "/best\" ";
            }
            cmd += "--merge-output-format " + chosenExt + " --remux-video " + chosenExt + " ";
        }

        cmd += "-o \"%(title)s.%(ext)s\" \"" + std::string(g_app.url_input) + "\"";

        g_app.progress = DownloadProgress();
        g_app.is_busy = true;
        std::thread(WorkerDownload, cmd, targetFolder).detach();
    }
    if (!canDownload) ImGui::EndDisabled();

    ImGui::Spacing();

    // Live Download Progress & Stats Card
    ImGui::BeginChild("ProgressCard", ImVec2(0, 85), true, cardFlags);
    DownloadProgress dp;
    {
        std::lock_guard<std::mutex> lock(g_app.progress_mutex);
        dp = g_app.progress;
    }

    if (!dp.total_str.empty()) {
        ImGui::TextColored(ImVec4(0.133f, 0.827f, 0.933f, 1.0f), "%.1f%%", dp.percent);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.945f, 0.961f, 0.976f, 1.0f), "• %s / %s", dp.downloaded_str.c_str(), dp.total_str.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.956f, 0.447f, 0.714f, 1.0f), "(Left: %s)", dp.left_str.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.204f, 0.827f, 0.600f, 1.0f), "• %s", dp.speed_str.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.545f, 0.361f, 0.965f, 1.0f), "• ETA %s", dp.eta_str.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.580f, 0.639f, 0.722f, 1.0f), "Status: %s", g_app.status_message.c_str());
    }

    float progFraction = (float)(dp.percent / 100.0);
    progFraction = std::max(0.0f, std::min(1.0f, progFraction));
    ImGui::ProgressBar(progFraction, ImVec2(-1, 16), "");
    ImGui::EndChild();

    // Process Console Log Card
    ImGui::TextColored(ImVec4(0.580f, 0.639f, 0.722f, 1.0f), "Process Console Log");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 65.0f);
    if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> lock(g_app.log_mutex);
        g_app.log_lines.clear();
    }

    ImGui::BeginChild("LogCard", ImVec2(0, -1), true);
    {
        std::lock_guard<std::mutex> lock(g_app.log_mutex);
        for (const auto& l : g_app.log_lines) {
            if (l.find("[error]") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.956f, 0.447f, 0.714f, 1.0f), "%s", l.c_str());
            } else if (l.find("[success]") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.204f, 0.827f, 0.600f, 1.0f), "%s", l.c_str());
            } else if (l.find("[download]") != std::string::npos || l.find("[folder]") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.133f, 0.827f, 0.933f, 1.0f), "%s", l.c_str());
            } else {
                ImGui::TextUnformatted(l.c_str());
            }
        }
        if (g_app.scroll_to_bottom) {
            ImGui::SetScrollHereY(1.0f);
            g_app.scroll_to_bottom = false;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

// WinMain Entry Point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Initial default download folder (user's Downloads\StreamFetch folder or current dir)
    PWSTR pDownloads = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &pDownloads))) {
        int len = WideCharToMultiByte(CP_UTF8, 0, pDownloads, -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            std::string dFolder(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, pDownloads, -1, &dFolder[0], len, NULL, NULL);
            std::string target = dFolder + "\\StreamFetch";
            strncpy(g_app.folder_input, target.c_str(), sizeof(g_app.folder_input) - 1);
        }
        CoTaskMemFree(pDownloads);
    } else {
        strcpy(g_app.folder_input, ".\\downloads");
    }

    // Register Window Class
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, hInstance,
                      NULL, NULL, NULL, NULL, _T("StreamFetch_App"), NULL };
    RegisterClassEx(&wc);

    // Initial Window Size & Position
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int width = std::min(820, screenW - 60);
    int height = std::min(740, screenH - 100);
    int posX = std::max(20, (screenW - width) / 2);
    int posY = std::max(30, (screenH - height) / 2);

    HWND hwnd = CreateWindow(
        wc.lpszClassName, _T("StreamFetch"),
        WS_OVERLAPPEDWINDOW, posX, posY, width, height,
        NULL, NULL, wc.hInstance, NULL
    );
    g_hWnd = hwnd;

    // Enable Immersive Dark Mode for Title Bar
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load High-DPI Windows Segoe UI Fonts
    ImFontConfig font_cfg;
    font_cfg.OversampleH = 3;
    font_cfg.OversampleV = 2;
    font_cfg.PixelSnapH = true;

    if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        g_fontRegular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.5f, &font_cfg);
    }
    if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeuib.ttf")) {
        g_fontTitle = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 23.0f, &font_cfg);
        g_fontBold  = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 17.5f, &font_cfg);
    }
    if (!g_fontRegular) {
        g_fontRegular = io.Fonts->AddFontDefault();
    }

    SetupElectricSocialStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clear_color = ImVec4(0.031f, 0.059f, 0.125f, 1.00f); // #080F20 Deep Navy

    // Main Loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w,
                                                  clear_color.y * clear_color.w,
                                                  clear_color.z * clear_color.w,
                                                  clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync enabled for smooth 60 FPS
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    CoUninitialize();

    return 0;
}

// Helper Functions for DirectX 11
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                               createDeviceFlags, featureLevelArray, 2,
                                               D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                               &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) {
        // Automatic fallback to Windows WARP (high-speed CPU rasterizer built into every Windows installation)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                            createDeviceFlags, featureLevelArray, 2,
                                            D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Win32 Message Handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
