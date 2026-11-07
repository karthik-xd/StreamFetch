#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <regex>
#include <array>
#include <algorithm>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

enum class DownloadMode {
    VideoWithAudio = 1,
    VideoOnly = 2,
    AudioOnly = 3
};

struct VideoMetadata {
    std::string title;
    std::vector<int> resolutions;
};

std::string run_capture(const std::string& cmd) {
    std::array<char, 512> buffer;
    std::string result;
    FILE* pipe = POPEN(cmd.c_str(), "r");
    if (!pipe) return "";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    PCLOSE(pipe);
    return result;
}

VideoMetadata fetch_metadata(const std::string& url) {
    VideoMetadata meta;
    std::string cmd = "yt-dlp --print \"%(title)s\" --print \"%(duration)s\" -F --no-playlist \"" + url + "\"";
    std::string output = run_capture(cmd);

    if (output.empty()) return meta;

    std::regex res_pattern(R"((\d{3,4})x(\d{3,4})|\b(\d{3,4})p(?:60)?)");
    auto begin = std::sregex_iterator(output.begin(), output.end(), res_pattern);
    auto end = std::sregex_iterator();

    std::set<int> unique_heights;
    for (std::sregex_iterator i = begin; i != end; ++i) {
        std::smatch match = *i;
        int height = 0;
        if (match[2].matched) height = std::stoi(match[2].str());
        else if (match[3].matched) height = std::stoi(match[3].str());
        else if (match[1].matched) height = std::stoi(match[1].str());

        if (height >= 144) {
            unique_heights.insert(height);
        }
    }

    for (auto it = unique_heights.rbegin(); it != unique_heights.rend(); ++it) {
        meta.resolutions.push_back(*it);
    }

    return meta;
}

std::string build_command(const std::string& url, DownloadMode mode, int height, const std::string& slice_section = "", const std::string& output_template = "%(title)s.%(ext)s") {
    std::string cmd = "yt-dlp --newline --no-mtime ";
    cmd += "--http-chunk-size 10M ";
    cmd += "--concurrent-fragments 4 ";
    cmd += "--retries 25 --fragment-retries 25 --file-access-retries 10 ";
    cmd += "--retry-sleep fragment:exp=1:10 ";
    cmd += "--buffer-size 16M ";
    cmd += "--js-runtimes \"node:C:\\node js\\node.exe\" ";

    if (!slice_section.empty()) {
        cmd += slice_section + " ";
    }

    if (mode == DownloadMode::AudioOnly) {
        cmd += "-x --audio-format mp3 -f \"bestaudio/best\" ";
    } else {
        std::string height_filter = (height > 0) ? "[height<=" + std::to_string(height) + "]" : "";
        if (mode == DownloadMode::VideoOnly) {
            cmd += "-f \"bestvideo" + height_filter + "\" ";
        } else {
            cmd += "-f \"bestvideo" + height_filter + "+bestaudio/best" + height_filter + "/best\" ";
        }
    }

    cmd += "-o \"" + output_template + "\" ";
    cmd += "\"" + url + "\"";
    return cmd;
}

bool execute_download(const std::string& cmd) {
    std::cout << "\nExecuting: " << cmd << "\n\n";

    std::array<char, 256> buffer;
    FILE* pipe = POPEN(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Failed to start process.\n";
        return false;
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        std::cout << buffer.data();
    }

    int return_code = PCLOSE(pipe);
    return (return_code == 0);
}

int main() {
    std::cout << "========================================\n";
    std::cout << "      StreamFetch (CLI Edition)        \n";
    std::cout << "========================================\n\n";

    std::string url;
    std::cout << "Enter YouTube URL: ";
    if (!std::getline(std::cin, url) || url.empty()) {
        std::cerr << "URL cannot be empty.\n";
        return 1;
    }

    std::cout << "\nFetching video formats from YouTube...\n";
    VideoMetadata meta = fetch_metadata(url);

    std::cout << "\nSelect Download Mode:\n";
    std::cout << "  1. Video with Audio\n";
    std::cout << "  2. Video without Audio\n";
    std::cout << "  3. Audio Only (MP3)\n";
    std::cout << "Choice (1-3): ";

    int mode_choice = 1;
    if (!(std::cin >> mode_choice) || mode_choice < 1 || mode_choice > 3) {
        std::cerr << "Invalid choice. Defaulting to Video with Audio.\n";
        mode_choice = 1;
    }

    DownloadMode mode = static_cast<DownloadMode>(mode_choice);
    int selected_height = 0;

    if (mode != DownloadMode::AudioOnly) {
        if (!meta.resolutions.empty()) {
            int max_q = meta.resolutions.front();
            int min_q = meta.resolutions.back();

            std::cout << "\nDetected Video Qualities (Min: " << min_q << "p | Max: " << max_q << "p):\n";
            std::cout << "  1. Best available (" << max_q << "p)\n";

            for (size_t i = 0; i < meta.resolutions.size(); ++i) {
                int res = meta.resolutions[i];
                std::cout << "  " << (i + 2) << ". " << res << "p";
                if (res == max_q) std::cout << " (Max)";
                if (res == min_q) std::cout << " (Min)";
                std::cout << "\n";
            }

            std::cout << "Choice (1-" << (meta.resolutions.size() + 1) << "): ";
            size_t q_choice = 1;
            if (std::cin >> q_choice && q_choice >= 2 && q_choice <= meta.resolutions.size() + 1) {
                selected_height = meta.resolutions[q_choice - 2];
            }
        }
    }

    // Time Slice Section
    std::string slice_arg = "";
    std::cout << "\nDownload slice only? (y/N): ";
    std::string slice_resp;
    if (std::cin >> slice_resp && (slice_resp == "y" || slice_resp == "Y")) {
        std::string start_t, end_t;
        std::cout << "Enter start timestamp (e.g. 00:01:30 or 90): ";
        std::cin >> start_t;
        std::cout << "Enter end timestamp (e.g. 00:02:45 or 165): ";
        std::cin >> end_t;
        if (start_t.empty()) start_t = "00:00:00";
        if (end_t.empty()) end_t = "inf";
        slice_arg = "--download-sections \"*" + start_t + "-" + end_t + "\"";
    }

    std::string cmd = build_command(url, mode, selected_height, slice_arg);
    if (!execute_download(cmd)) {
        std::cerr << "\nDownload failed.\n";
        return 1;
    }

    std::cout << "\nDownload complete.\n";
    return 0;
}
