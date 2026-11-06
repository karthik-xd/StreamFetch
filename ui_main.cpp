#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <regex>
#include <thread>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Palette: Electric Social
#define COLOR_DEEP_NAVY   RGB(8, 15, 32)      // #080F20 - Main window background
#define COLOR_SURFACE     RGB(17, 28, 50)     // #111C32 - Card / Panel surface
#define COLOR_INPUT_BG    RGB(22, 34, 60)     // #16223C - Input background
#define COLOR_BORDER      RGB(30, 41, 59)     // #1E293B - Card border
#define COLOR_ELECTRIC    RGB(59, 130, 246)   // #3B82F6 - Electric Blue
#define COLOR_ELECTRIC_HV RGB(37, 99, 235)   // #2563EB - Electric Blue Hover
#define COLOR_NEON_CYAN   RGB(34, 211, 238)   // #22D3EE - Neon Cyan
#define COLOR_VIOLET      RGB(139, 92, 246)   // #8B5CF6 - Violet
#define COLOR_SOFT_WHITE  RGB(241, 245, 249)  // #F1F5F9 - Primary Text
#define COLOR_MUTED_TEXT  RGB(148, 163, 184)  // #94A3B8 - Secondary Muted Text
#define COLOR_SUCCESS     RGB(52, 211, 153)   // #34D399 - Success / Complete
#define COLOR_LIKE_PINK   RGB(244, 114, 182)  // #F472B6 - Like Pink (Badge / Slicing indicator)

// Control IDs
#define IDC_EDIT_URL          101
#define IDC_BTN_FETCH         102
#define IDC_STATIC_TITLE      103
#define IDC_RADIO_WITH_AUDIO  104
#define IDC_RADIO_VIDEO_ONLY  105
#define IDC_RADIO_AUDIO_ONLY  106
#define IDC_COMBO_QUALITY     107
#define IDC_CHK_SLICE         108
#define IDC_SLIDER_RANGE      109
#define IDC_BTN_DOWNLOAD      110
#define IDC_MODERN_PROGRESS   111
#define IDC_STATIC_STATS      112
#define IDC_EDIT_LOG          113

// Custom Messages
#define WM_METADATA_READY     (WM_USER + 1)
#define WM_LOG_APPEND         (WM_USER + 2)
#define WM_DOWNLOAD_DONE      (WM_USER + 3)
#define WM_PROGRESS_UPDATE    (WM_USER + 4)
#define WM_SLIDER_CHANGED     (WM_USER + 5)

struct QualityOption {
    int height = 0;
    std::string raw_note;
    std::string size_str;
    std::wstring display_label;
};

struct DownloadProgress {
    double percent = 0.0;
    std::wstring downloaded_str;
    std::wstring total_str;
    std::wstring left_str;
    std::wstring speed_str;
    std::wstring eta_str;
};

struct RangeSliderData {
    int duration_sec = 60;
    int start_sec = 0;
    int end_sec = 60;
    int active_thumb = 0; // 0 = none, 1 = start, 2 = end
    int margin_x = 16;
    int track_y = 16;
    int thumb_radius = 8;
};

struct AppState {
    HWND hMainWnd = NULL;
    HWND hEditUrl = NULL;
    HWND hBtnFetch = NULL;
    HWND hStaticTitle = NULL;
    HWND hGroupMode = NULL;
    HWND hRadioWithAudio = NULL;
    HWND hRadioVideoOnly = NULL;
    HWND hRadioAudioOnly = NULL;
    HWND hLabelQuality = NULL;
    HWND hComboQuality = NULL;

    HWND hGroupSlice = NULL;
    HWND hChkSlice = NULL;
    HWND hSliderRange = NULL;

    HWND hBtnDownload = NULL;
    HWND hModernProgress = NULL;
    HWND hStaticStats = NULL;
    HWND hEditLog = NULL;

    HFONT hFontRegular = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontTitle = NULL;
    HFONT hFontConsole = NULL;
    HFONT hFontSmall = NULL;
    HFONT hFontStats = NULL;

    HBRUSH hBrushBg = NULL;
    HBRUSH hBrushSurface = NULL;
    HBRUSH hBrushInput = NULL;
    HBRUSH hBrushBorder = NULL;

    RangeSliderData sliderData;
    std::vector<QualityOption> available_qualities;
    std::string audio_size_str;
    std::string video_title;
    int video_duration = 0;
    double current_progress_percent = 0.0;
    DownloadProgress last_progress;

    std::atomic<bool> is_busy{false};
};

AppState g_app;

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size);
    return wstr;
}

std::wstring FormatTime(int total_sec) {
    if (total_sec < 0) total_sec = 0;
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;
    wchar_t buf[32];
    if (h > 0) {
        swprintf(buf, 32, L"%02d:%02d:%02d", h, m, s);
    } else {
        swprintf(buf, 32, L"%02d:%02d", m, s);
    }
    return buf;
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

std::wstring FormatMB(double mb) {
    wchar_t buf[32];
    if (mb >= 1024.0) {
        swprintf(buf, 32, L"%.2f GiB", mb / 1024.0);
    } else {
        swprintf(buf, 32, L"%.2f MiB", mb);
    }
    return buf;
}

void AppendLog(HWND hLog, const std::wstring& text) {
    int len = GetWindowTextLength(hLog);
    // Prevent buffer exhaustion from blocking pipe
    if (len > 32000) {
        SendMessage(hLog, EM_SETSEL, 0, 16000);
        SendMessage(hLog, EM_REPLACESEL, 0, (LPARAM)L"[... log history trimmed ...]\r\n");
        len = GetWindowTextLength(hLog);
    }
    SendMessage(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hLog, EM_REPLACESEL, 0, (LPARAM)text.c_str());
    SendMessage(hLog, EM_SCROLLCARET, 0, 0);
}

// Custom Dual-Thumb Range Slider
LRESULT CALLBACK SliderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RangeSliderData* data = (RangeSliderData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        HBRUSH bgBrush = CreateSolidBrush(COLOR_SURFACE);
        FillRect(memDC, &rc, bgBrush);
        DeleteObject(bgBrush);

        if (!data || data->duration_sec <= 0) {
            SelectObject(memDC, oldBM);
            DeleteObject(memBM);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        int trackLeft = data->margin_x;
        int trackRight = rc.right - data->margin_x;
        int trackWidth = trackRight - trackLeft;
        int trackY = data->track_y;
        int trackHeight = 6;

        // Base Track (Deep Navy)
        RECT rcBase = { trackLeft, trackY - trackHeight / 2, trackRight, trackY + trackHeight / 2 };
        HBRUSH baseTrackBrush = CreateSolidBrush(COLOR_DEEP_NAVY);
        FillRect(memDC, &rcBase, baseTrackBrush);
        DeleteObject(baseTrackBrush);

        int xStart = trackLeft + (int)((double)data->start_sec / data->duration_sec * trackWidth);
        int xEnd = trackLeft + (int)((double)data->end_sec / data->duration_sec * trackWidth);

        // Active Track (Neon Cyan)
        RECT rcActive = { xStart, trackY - trackHeight / 2, xEnd, trackY + trackHeight / 2 };
        HBRUSH activeTrackBrush = CreateSolidBrush(COLOR_NEON_CYAN);
        FillRect(memDC, &rcActive, activeTrackBrush);
        DeleteObject(activeTrackBrush);

        // Start Thumb (Electric Blue)
        HBRUSH startThumbBrush = CreateSolidBrush(COLOR_ELECTRIC);
        HPEN whitePen = CreatePen(PS_SOLID, 2, COLOR_SOFT_WHITE);
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, startThumbBrush);
        HPEN oldPen = (HPEN)SelectObject(memDC, whitePen);

        Ellipse(memDC,
            xStart - data->thumb_radius, trackY - data->thumb_radius,
            xStart + data->thumb_radius, trackY + data->thumb_radius);

        // End Thumb (Violet)
        HBRUSH endThumbBrush = CreateSolidBrush(COLOR_VIOLET);
        SelectObject(memDC, endThumbBrush);

        Ellipse(memDC,
            xEnd - data->thumb_radius, trackY - data->thumb_radius,
            xEnd + data->thumb_radius, trackY + data->thumb_radius);

        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(startThumbBrush);
        DeleteObject(endThumbBrush);
        DeleteObject(whitePen);

        // Time Labels
        SetBkMode(memDC, TRANSPARENT);
        SelectObject(memDC, g_app.hFontSmall);

        std::wstring strStart = L"Start: " + FormatTime(data->start_sec);
        SetTextColor(memDC, COLOR_NEON_CYAN);
        TextOut(memDC, trackLeft, trackY + 12, strStart.c_str(), (int)strStart.size());

        int sliceDuration = data->end_sec - data->start_sec;
        std::wstring strDuration = L"Slice: " + FormatTime(sliceDuration);
        SetTextColor(memDC, COLOR_SOFT_WHITE);
        SIZE sz;
        GetTextExtentPoint32(memDC, strDuration.c_str(), (int)strDuration.size(), &sz);
        TextOut(memDC, (rc.right - sz.cx) / 2, trackY + 12, strDuration.c_str(), (int)strDuration.size());

        std::wstring strEnd = L"End: " + FormatTime(data->end_sec);
        SetTextColor(memDC, COLOR_VIOLET);
        SIZE szEnd;
        GetTextExtentPoint32(memDC, strEnd.c_str(), (int)strEnd.size(), &szEnd);
        TextOut(memDC, trackRight - szEnd.cx, trackY + 12, strEnd.c_str(), (int)strEnd.size());

        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!data || data->duration_sec <= 0) return 0;
        int mouseX = LOWORD(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int trackLeft = data->margin_x;
        int trackRight = rc.right - data->margin_x;
        int trackWidth = trackRight - trackLeft;

        int xStart = trackLeft + (int)((double)data->start_sec / data->duration_sec * trackWidth);
        int xEnd = trackLeft + (int)((double)data->end_sec / data->duration_sec * trackWidth);

        int distStart = abs(mouseX - xStart);
        int distEnd = abs(mouseX - xEnd);

        data->active_thumb = (distStart <= distEnd) ? 1 : 2;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!data || data->active_thumb == 0 || !(wParam & MK_LBUTTON)) return 0;

        int mouseX = LOWORD(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int trackLeft = data->margin_x;
        int trackRight = rc.right - data->margin_x;
        int trackWidth = trackRight - trackLeft;

        int clampedX = std::max(trackLeft, std::min(trackRight, mouseX));
        double ratio = (double)(clampedX - trackLeft) / (double)trackWidth;
        int targetSec = (int)(ratio * data->duration_sec);

        if (data->active_thumb == 1) {
            data->start_sec = std::max(0, std::min(targetSec, data->end_sec - 1));
        } else if (data->active_thumb == 2) {
            data->end_sec = std::min(data->duration_sec, std::max(targetSec, data->start_sec + 1));
        }

        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (data && data->active_thumb != 0) {
            data->active_thumb = 0;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Custom Modern Progress Bar
LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        // Background track (Surface)
        HBRUSH bgBrush = CreateSolidBrush(COLOR_SURFACE);
        FillRect(memDC, &rc, bgBrush);
        DeleteObject(bgBrush);

        // Border
        HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
        HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
        SelectObject(memDC, GetStockObject(NULL_BRUSH));
        RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        SelectObject(memDC, oldPen);
        DeleteObject(borderPen);

        // Fill progress
        double percent = g_app.current_progress_percent;
        if (percent > 0.0) {
            if (percent > 100.0) percent = 100.0;
            int fillWidth = (int)((rc.right - rc.left - 2) * (percent / 100.0));
            if (fillWidth > 0) {
                RECT rcFill = { rc.left + 1, rc.top + 1, rc.left + 1 + fillWidth, rc.bottom - 1 };
                HBRUSH fillBrush = CreateSolidBrush(COLOR_NEON_CYAN);
                FillRect(memDC, &rcFill, fillBrush);
                DeleteObject(fillBrush);
            }
        }

        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void FetchMetadataThread(HWND hwnd, std::string url) {
    std::string cmd = "yt-dlp --print \"%(title)s\" --print \"%(duration)s\" -F --no-playlist \"" + url + "\"";
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        SendMessage(hwnd, WM_METADATA_READY, 0, 0);
        return;
    }

    char buffer[1024];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    _pclose(pipe);

    std::string* pOutput = new std::string(output);
    PostMessage(hwnd, WM_METADATA_READY, (WPARAM)pOutput, 0);
}

void DownloadThread(HWND hwnd, std::string cmd) {
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        PostMessage(hwnd, WM_DOWNLOAD_DONE, 1, 0);
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
            // Throttle UI posts to at most once per 150ms to prevent pipe buffer congestion!
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() >= 150) {
                last_update = now;

                DownloadProgress* dp = new DownloadProgress();
                dp->percent = std::stod(m[1].str());
                dp->total_str = Utf8ToWide(m[2].str());
                dp->speed_str = Utf8ToWide(m[3].str());
                dp->eta_str = Utf8ToWide(m[4].str());

                double totalMB = ParseSizeToMB(m[2].str());
                double downloadedMB = totalMB * (dp->percent / 100.0);
                double leftMB = std::max(0.0, totalMB - downloadedMB);

                dp->downloaded_str = FormatMB(downloadedMB);
                dp->left_str = FormatMB(leftMB);

                PostMessage(hwnd, WM_PROGRESS_UPDATE, (WPARAM)dp, 0);
            }
        } else {
            // Only send significant log lines (not download progress ticks or bare carriage returns)
            if (line.find("[download]") == std::string::npos && line.find("\r") == std::string::npos && line.size() > 2) {
                std::wstring* wLine = new std::wstring(Utf8ToWide(line));
                PostMessage(hwnd, WM_LOG_APPEND, (WPARAM)wLine, 0);
            }
        }
    }

    int code = _pclose(pipe);
    PostMessage(hwnd, WM_DOWNLOAD_DONE, (WPARAM)code, 0);
}

void UpdateSliceVisibility() {
    bool isSlice = (SendMessage(g_app.hChkSlice, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ShowWindow(g_app.hSliderRange, isSlice ? SW_SHOW : SW_HIDE);

    if (isSlice) {
        SetWindowText(g_app.hBtnDownload, L"Download Video Slice");
    } else {
        bool isAudio = (SendMessage(g_app.hRadioAudioOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
        SetWindowText(g_app.hBtnDownload, isAudio ? L"Download Audio (MP3)" : L"Download Video");
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

        g_app.hBrushBg = CreateSolidBrush(COLOR_DEEP_NAVY);
        g_app.hBrushSurface = CreateSolidBrush(COLOR_SURFACE);
        g_app.hBrushInput = CreateSolidBrush(COLOR_INPUT_BG);
        g_app.hBrushBorder = CreateSolidBrush(COLOR_BORDER);

        g_app.hFontTitle = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_app.hFontBold = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_app.hFontRegular = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_app.hFontStats = CreateFontW(13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_app.hFontSmall = CreateFontW(12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_app.hFontConsole = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Consolas");

        // Header
        HWND hTitle = CreateWindowEx(0, L"STATIC", L"YouTube Downloader (Electric Edition)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 16, 450, 26, hwnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)g_app.hFontTitle, TRUE);

        // URL Label & Input
        HWND hLabelUrl = CreateWindowEx(0, L"STATIC", L"Video URL",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 48, 100, 18, hwnd, NULL, NULL, NULL);
        SendMessage(hLabelUrl, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

        g_app.hEditUrl = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            24, 68, 440, 32, hwnd, (HMENU)IDC_EDIT_URL, NULL, NULL);
        SendMessage(g_app.hEditUrl, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);

        g_app.hBtnFetch = CreateWindowEx(0, L"BUTTON", L"Fetch Info",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            472, 68, 120, 32, hwnd, (HMENU)IDC_BTN_FETCH, NULL, NULL);

        // Video Title display
        g_app.hStaticTitle = CreateWindowEx(0, L"STATIC", L"Paste YouTube URL and click Fetch Info to load duration, qualities, and sizes",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            24, 108, 568, 20, hwnd, (HMENU)IDC_STATIC_TITLE, NULL, NULL);
        SendMessage(g_app.hStaticTitle, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);

        // Mode Group
        g_app.hGroupMode = CreateWindowEx(0, L"BUTTON", L"Download Mode",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            24, 130, 568, 54, hwnd, NULL, NULL, NULL);
        SendMessage(g_app.hGroupMode, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

        g_app.hRadioWithAudio = CreateWindowEx(0, L"BUTTON", L"Video + Audio",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            40, 150, 140, 24, hwnd, (HMENU)IDC_RADIO_WITH_AUDIO, NULL, NULL);
        SendMessage(g_app.hRadioWithAudio, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);
        SendMessage(g_app.hRadioWithAudio, BM_SETCHECK, BST_CHECKED, 0);

        g_app.hRadioVideoOnly = CreateWindowEx(0, L"BUTTON", L"Video Only",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            215, 150, 130, 24, hwnd, (HMENU)IDC_RADIO_VIDEO_ONLY, NULL, NULL);
        SendMessage(g_app.hRadioVideoOnly, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);

        g_app.hRadioAudioOnly = CreateWindowEx(0, L"BUTTON", L"Audio Only (MP3)",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            380, 150, 160, 24, hwnd, (HMENU)IDC_RADIO_AUDIO_ONLY, NULL, NULL);
        SendMessage(g_app.hRadioAudioOnly, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);

        // Quality Dropdown
        g_app.hLabelQuality = CreateWindowEx(0, L"STATIC", L"Stream Quality & Estimated Size:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 196, 230, 20, hwnd, NULL, NULL, NULL);
        SendMessage(g_app.hLabelQuality, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

        g_app.hComboQuality = CreateWindowEx(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            250, 192, 342, 250, hwnd, (HMENU)IDC_COMBO_QUALITY, NULL, NULL);
        SendMessage(g_app.hComboQuality, WM_SETFONT, (WPARAM)g_app.hFontRegular, TRUE);
        SendMessage(g_app.hComboQuality, CB_ADDSTRING, 0, (LPARAM)L"Best Available");
        SendMessage(g_app.hComboQuality, CB_SETCURSEL, 0, 0);

        // Slicing Group Box
        g_app.hGroupSlice = CreateWindowEx(0, L"BUTTON", L"Video Slicing (Dual-Point Seekbar)",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            24, 228, 568, 102, hwnd, NULL, NULL, NULL);
        SendMessage(g_app.hGroupSlice, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

        // Checkbox: Slice
        g_app.hChkSlice = CreateWindowEx(0, L"BUTTON", L"Slice (Download custom section of video)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            40, 250, 320, 22, hwnd, (HMENU)IDC_CHK_SLICE, NULL, NULL);
        SendMessage(g_app.hChkSlice, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

        // Custom Dual-Thumb Range Slider
        g_app.sliderData.duration_sec = 60;
        g_app.sliderData.start_sec = 0;
        g_app.sliderData.end_sec = 60;

        g_app.hSliderRange = CreateWindowEx(
            0, L"DualRangeSlider", NULL,
            WS_CHILD | WS_CLIPSIBLINGS,
            40, 274, 536, 48, hwnd, (HMENU)IDC_SLIDER_RANGE, NULL, &g_app.sliderData
        );

        // Download Action Button
        g_app.hBtnDownload = CreateWindowEx(0, L"BUTTON", L"Download Video",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            24, 340, 568, 38, hwnd, (HMENU)IDC_BTN_DOWNLOAD, NULL, NULL);

        // Live Download Stats Card
        g_app.hStaticStats = CreateWindowEx(0, L"STATIC", L"Download Status: Idle",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 386, 568, 22, hwnd, (HMENU)IDC_STATIC_STATS, NULL, NULL);
        SendMessage(g_app.hStaticStats, WM_SETFONT, (WPARAM)g_app.hFontStats, TRUE);

        // Custom Modern Progress Bar
        g_app.hModernProgress = CreateWindowEx(
            0, L"ModernProgressBar", NULL,
            WS_CHILD | WS_VISIBLE,
            24, 412, 568, 12, hwnd, (HMENU)IDC_MODERN_PROGRESS, NULL, NULL
        );

        // Console Log Output
        HWND hLabelLog = CreateWindowEx(0, L"STATIC", L"Process Output Log:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 432, 200, 18, hwnd, NULL, NULL, NULL);
        SendMessage(hLabelLog, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

        g_app.hEditLog = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"Ready.\r\n",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
            24, 452, 568, 200, hwnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
        SendMessage(g_app.hEditLog, WM_SETFONT, (WPARAM)g_app.hFontConsole, TRUE);

        UpdateSliceVisibility();
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_BTN_FETCH || dis->CtlID == IDC_BTN_DOWNLOAD) {
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;

            bool isSelected = (dis->itemState & ODS_SELECTED);
            HBRUSH btnBrush = CreateSolidBrush(isSelected ? COLOR_ELECTRIC_HV : COLOR_ELECTRIC);
            FillRect(hdc, &rc, btnBrush);
            DeleteObject(btnBrush);

            HPEN pen = CreatePen(PS_SOLID, 1, COLOR_NEON_CYAN);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

            wchar_t text[128];
            GetWindowText(dis->hwndItem, text, 128);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COLOR_SOFT_WHITE);
            SelectObject(hdc, g_app.hFontBold);
            DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_app.hStaticTitle) {
            SetTextColor(hdc, COLOR_MUTED_TEXT);
        } else if (hCtrl == g_app.hStaticStats) {
            SetTextColor(hdc, COLOR_NEON_CYAN);
        } else {
            SetTextColor(hdc, COLOR_SOFT_WHITE);
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_app.hBrushBg;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COLOR_SOFT_WHITE);
        SetBkColor(hdc, COLOR_INPUT_BG);
        return (LRESULT)g_app.hBrushInput;
    }

    case WM_CTLCOLORBTN: {
        return (LRESULT)g_app.hBrushBg;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_CHK_SLICE && code == BN_CLICKED) {
            UpdateSliceVisibility();
        }

        if (id == IDC_RADIO_AUDIO_ONLY && code == BN_CLICKED) {
            EnableWindow(g_app.hComboQuality, FALSE);
            UpdateSliceVisibility();
            if (!g_app.audio_size_str.empty()) {
                std::wstring msg = L"Audio Stream ~ " + Utf8ToWide(g_app.audio_size_str);
                SetWindowText(g_app.hStaticStats, msg.c_str());
            }
        } else if ((id == IDC_RADIO_WITH_AUDIO || id == IDC_RADIO_VIDEO_ONLY) && code == BN_CLICKED) {
            EnableWindow(g_app.hComboQuality, TRUE);
            UpdateSliceVisibility();
        }

        if (id == IDC_BTN_FETCH && code == BN_CLICKED) {
            if (g_app.is_busy.load()) return 0;

            wchar_t wUrl[1024];
            GetWindowText(g_app.hEditUrl, wUrl, 1024);
            std::string url = WideToUtf8(wUrl);
            if (url.empty()) {
                MessageBox(hwnd, L"Please enter a valid YouTube URL.", L"Missing URL", MB_ICONWARNING);
                return 0;
            }

            g_app.is_busy.store(true);
            EnableWindow(g_app.hBtnFetch, FALSE);
            EnableWindow(g_app.hBtnDownload, FALSE);
            SetWindowText(g_app.hStaticTitle, L"Analyzing video streams, formats, and file sizes...");
            SetWindowText(g_app.hStaticStats, L"Fetching metadata from YouTube...");
            AppendLog(g_app.hEditLog, L"Querying formats and file sizes from YouTube...\r\n");

            std::thread(FetchMetadataThread, hwnd, url).detach();
        }

        if (id == IDC_BTN_DOWNLOAD && code == BN_CLICKED) {
            if (g_app.is_busy.load()) return 0;

            wchar_t wUrl[1024];
            GetWindowText(g_app.hEditUrl, wUrl, 1024);
            std::string url = WideToUtf8(wUrl);
            if (url.empty()) {
                MessageBox(hwnd, L"Please enter a valid YouTube URL.", L"Missing URL", MB_ICONWARNING);
                return 0;
            }

            bool isAudioOnly = (SendMessage(g_app.hRadioAudioOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool isVideoOnly = (SendMessage(g_app.hRadioVideoOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool isSlice = (SendMessage(g_app.hChkSlice, BM_GETCHECK, 0, 0) == BST_CHECKED);

            int selected_height = 0;
            int sel = (int)SendMessage(g_app.hComboQuality, CB_GETCURSEL, 0, 0);
            if (sel > 0 && sel - 1 < (int)g_app.available_qualities.size()) {
                selected_height = g_app.available_qualities[sel - 1].height;
            }

            // High-stability streaming flags: chunking prevents YouTube CDN 3GB socket drops
            std::string cmd = "yt-dlp --newline --no-mtime ";
            cmd += "--http-chunk-size 10M ";
            cmd += "--concurrent-fragments 4 ";
            cmd += "--retries 25 --fragment-retries 25 --file-access-retries 10 ";
            cmd += "--retry-sleep fragment:exp=1:10 ";
            cmd += "--buffer-size 16M ";
            cmd += "--js-runtimes \"node:C:\\node js\\node.exe\" ";

            if (isSlice) {
                int startSec = g_app.sliderData.start_sec;
                int endSec = g_app.sliderData.end_sec;
                std::string sStart = std::to_string(startSec);
                std::string sEnd = std::to_string(endSec);
                cmd += "--download-sections \"*" + sStart + "-" + sEnd + "\" ";
            }

            if (isAudioOnly) {
                cmd += "-x --audio-format mp3 -f \"bestaudio/best\" ";
            } else {
                std::string height_filter = (selected_height > 0) ? "[height<=" + std::to_string(selected_height) + "]" : "";
                if (isVideoOnly) {
                    cmd += "-f \"bestvideo" + height_filter + "\" ";
                } else {
                    cmd += "-f \"bestvideo" + height_filter + "+bestaudio/best" + height_filter + "/best\" ";
                }
            }

            cmd += "-o \"%(title)s.%(ext)s\" \"" + url + "\"";

            g_app.is_busy.store(true);
            g_app.current_progress_percent = 0.0;
            InvalidateRect(g_app.hModernProgress, NULL, FALSE);
            EnableWindow(g_app.hBtnFetch, FALSE);
            EnableWindow(g_app.hBtnDownload, FALSE);
            SetWindowText(g_app.hStaticStats, L"Connecting to streaming server...");

            std::wstring startMsg = L"\r\n--- Starting Download (Chunked Continuous Mode) ---\r\nCommand: " + Utf8ToWide(cmd) + L"\r\n";
            AppendLog(g_app.hEditLog, startMsg);

            std::thread(DownloadThread, hwnd, cmd).detach();
        }
        return 0;
    }

    case WM_PROGRESS_UPDATE: {
        DownloadProgress* dp = (DownloadProgress*)wParam;
        if (dp) {
            g_app.current_progress_percent = dp->percent;
            InvalidateRect(g_app.hModernProgress, NULL, FALSE);

            std::wstringstream ss;
            ss << std::fixed << std::setprecision(1) << dp->percent << L"%";
            ss << L"  •  " << dp->downloaded_str << L" / " << dp->total_str;
            ss << L"  (Left: " << dp->left_str << L")";
            ss << L"  •  " << dp->speed_str;
            ss << L"  •  ETA " << dp->eta_str;

            SetWindowText(g_app.hStaticStats, ss.str().c_str());
            delete dp;
        }
        return 0;
    }

    case WM_METADATA_READY: {
        g_app.is_busy.store(false);
        EnableWindow(g_app.hBtnFetch, TRUE);
        EnableWindow(g_app.hBtnDownload, TRUE);

        std::string* pOutput = (std::string*)wParam;
        if (!pOutput || pOutput->empty()) {
            SetWindowText(g_app.hStaticTitle, L"Error: Failed to fetch metadata.");
            SetWindowText(g_app.hStaticStats, L"Failed to load video info.");
            delete pOutput;
            return 0;
        }

        std::string output = *pOutput;
        delete pOutput;

        std::istringstream stream(output);
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }

        if (lines.size() < 2) {
            SetWindowText(g_app.hStaticTitle, L"Error parsing format data.");
            return 0;
        }

        std::string durationStr = lines.back();
        std::string titleStr = lines[lines.size() - 2];

        g_app.video_title = titleStr;
        int dur = 0;
        try {
            dur = std::stoi(durationStr);
        } catch (...) {
            dur = 60;
        }
        if (dur <= 0) dur = 60;
        g_app.video_duration = dur;

        g_app.sliderData.duration_sec = dur;
        g_app.sliderData.start_sec = 0;
        g_app.sliderData.end_sec = dur;
        InvalidateRect(g_app.hSliderRange, NULL, FALSE);

        std::wstring wTitle = L"Title: " + Utf8ToWide(titleStr) + L"  (" + FormatTime(dur) + L")";
        SetWindowText(g_app.hStaticTitle, wTitle.c_str());

        std::map<int, QualityOption> qualityMap;
        std::regex vidRegex(R"((\d{3,4})x(\d{3,4})|\b(\d{3,4})p(?:60)?)", std::regex_constants::icase);
        std::regex sizeRegex(R"(\|\s*~?\s*(\d+(?:\.\d+)?\s*(?:KiB|MiB|GiB|kB|MB|GB|B)))", std::regex_constants::icase);

        std::string bestAudioSize = "";

        for (const auto& l : lines) {
            if (l.find("audio only") != std::string::npos) {
                std::smatch sm;
                if (std::regex_search(l, sm, sizeRegex)) {
                    bestAudioSize = sm[1].str();
                }
            }

            if (l.find("video only") != std::string::npos || l.find("avc1") != std::string::npos || l.find("vp9") != std::string::npos) {
                std::smatch vm;
                if (std::regex_search(l, vm, vidRegex)) {
                    int height = 0;
                    if (vm[2].matched) height = std::stoi(vm[2].str());
                    else if (vm[3].matched) height = std::stoi(vm[3].str());
                    else if (vm[1].matched) height = std::stoi(vm[1].str());

                    if (height >= 144) {
                        std::string sizeStr = "size unlisted";
                        std::smatch sm;
                        if (std::regex_search(l, sm, sizeRegex)) {
                            sizeStr = sm[1].str();
                        }

                        QualityOption opt;
                        opt.height = height;
                        opt.size_str = sizeStr;
                        qualityMap[height] = opt;
                    }
                }
            }
        }

        g_app.audio_size_str = bestAudioSize;
        g_app.available_qualities.clear();

        for (auto it = qualityMap.rbegin(); it != qualityMap.rend(); ++it) {
            g_app.available_qualities.push_back(it->second);
        }

        SendMessage(g_app.hComboQuality, CB_RESETCONTENT, 0, 0);
        if (!g_app.available_qualities.empty()) {
            int max_h = g_app.available_qualities.front().height;
            int min_h = g_app.available_qualities.back().height;

            std::wstring bestLbl = L"Best Available (" + std::to_wstring(max_h) + L"p)";
            if (!g_app.available_qualities.front().size_str.empty()) {
                bestLbl += L" ~ " + Utf8ToWide(g_app.available_qualities.front().size_str);
            }
            SendMessage(g_app.hComboQuality, CB_ADDSTRING, 0, (LPARAM)bestLbl.c_str());

            for (const auto& q : g_app.available_qualities) {
                std::wstring lbl = std::to_wstring(q.height) + L"p";
                if (q.height == max_h) lbl += L" (Max)";
                if (q.height == min_h) lbl += L" (Min)";
                if (!q.size_str.empty() && q.size_str != "size unlisted") {
                    lbl += L" ~ " + Utf8ToWide(q.size_str);
                }
                SendMessage(g_app.hComboQuality, CB_ADDSTRING, 0, (LPARAM)lbl.c_str());
            }

            std::wstring statusMsg = L"Loaded " + std::to_wstring(g_app.available_qualities.size()) +
                L" qualities (Max: " + std::to_wstring(max_h) + L"p ~ " + Utf8ToWide(g_app.available_qualities.front().size_str) +
                L"). Ready to download.";
            SetWindowText(g_app.hStaticStats, statusMsg.c_str());

            AppendLog(g_app.hEditLog, L"Formats discovered successfully.\r\n");
        } else {
            SendMessage(g_app.hComboQuality, CB_ADDSTRING, 0, (LPARAM)L"Best Available");
            SetWindowText(g_app.hStaticStats, L"Ready (default stream quality).");
        }
        SendMessage(g_app.hComboQuality, CB_SETCURSEL, 0, 0);

        return 0;
    }

    case WM_LOG_APPEND: {
        std::wstring* pLine = (std::wstring*)wParam;
        if (pLine) {
            AppendLog(g_app.hEditLog, *pLine);
            delete pLine;
        }
        return 0;
    }

    case WM_DOWNLOAD_DONE: {
        g_app.is_busy.store(false);
        EnableWindow(g_app.hBtnFetch, TRUE);
        EnableWindow(g_app.hBtnDownload, TRUE);

        int code = (int)wParam;
        if (code == 0) {
            g_app.current_progress_percent = 100.0;
            InvalidateRect(g_app.hModernProgress, NULL, FALSE);
            SetWindowText(g_app.hStaticStats, L"Download completed successfully (100%)!");
            AppendLog(g_app.hEditLog, L"\r\n=== Download Finished Successfully! ===\r\n");
            MessageBox(hwnd, L"Download completed successfully!", L"Success", MB_ICONINFORMATION);
        } else {
            SetWindowText(g_app.hStaticStats, L"Download failed. Check output log.");
            AppendLog(g_app.hEditLog, L"\r\n=== Download Failed (check log details) ===\r\n");
            MessageBox(hwnd, L"Download encountered an error.", L"Error", MB_ICONERROR);
        }
        return 0;
    }

    case WM_DESTROY: {
        DeleteObject(g_app.hBrushBg);
        DeleteObject(g_app.hBrushSurface);
        DeleteObject(g_app.hBrushInput);
        DeleteObject(g_app.hBrushBorder);
        DeleteObject(g_app.hFontTitle);
        DeleteObject(g_app.hFontBold);
        DeleteObject(g_app.hFontRegular);
        DeleteObject(g_app.hFontStats);
        DeleteObject(g_app.hFontSmall);
        DeleteObject(g_app.hFontConsole);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    // Register Custom Range Slider
    WNDCLASSEX wsc = { sizeof(WNDCLASSEX) };
    wsc.lpfnWndProc = SliderWndProc;
    wsc.hInstance = hInstance;
    wsc.lpszClassName = L"DualRangeSlider";
    wsc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wsc.hbrBackground = CreateSolidBrush(COLOR_SURFACE);
    RegisterClassEx(&wsc);

    // Register Modern Progress Bar
    WNDCLASSEX wpc = { sizeof(WNDCLASSEX) };
    wpc.lpfnWndProc = ProgressWndProc;
    wpc.hInstance = hInstance;
    wpc.lpszClassName = L"ModernProgressBar";
    wpc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wpc.hbrBackground = CreateSolidBrush(COLOR_SURFACE);
    RegisterClassEx(&wpc);

    // Register Main Window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ModernYTDownloader";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COLOR_DEEP_NAVY);

    if (!RegisterClassEx(&wc)) return 0;

    int winWidth = 632;
    int winHeight = 720;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;

    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"YouTube Downloader (Electric Edition)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
