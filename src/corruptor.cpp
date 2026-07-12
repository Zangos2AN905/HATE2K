#include "corruptor.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace hate2k {

std::vector<AssetCategory> all_categories() {
    return {
        {"CharSet",         "CharSet",         {".png", ".bmp", ".xyz"}},
        {"ChipSet",         "ChipSet",         {".png", ".bmp", ".xyz"}},
        {"FaceSet",         "FaceSet",         {".png", ".bmp", ".xyz"}},
        {"Monster",         "Monster",         {".png", ".bmp", ".xyz"}},
        {"Panorama",        "Panorama",        {".png", ".bmp", ".xyz"}},
        {"Picture",         "Picture",         {".png", ".bmp", ".xyz"}},
        {"System",          "System",          {".png", ".bmp", ".xyz"}},
        {"Title",           "Title",           {".png", ".bmp", ".xyz"}},
        {"Backdrop",        "Backdrop",        {".png", ".bmp", ".xyz"}},
        {"Battle",          "Battle",          {".png", ".bmp", ".xyz"}},
        {"Battle2",         "Battle2",         {".png", ".bmp", ".xyz"}},
        {"BattleCharSet",   "BattleCharSet",   {".png", ".bmp", ".xyz"}},
        {"BattleWeapon",    "BattleWeapon",    {".png", ".bmp", ".xyz"}},
        {"Frame",           "Frame",           {".png", ".bmp", ".xyz"}},
        {"GameOver",        "GameOver",        {".png", ".bmp", ".xyz"}},
        {"System2",         "System2",         {".png", ".bmp", ".xyz"}},
        {"Music",           "Music",           {".mid", ".midi", ".wav", ".mp3", ".ogg"}},
        {"Sound",           "Sound",           {".wav", ".mp3", ".ogg"}},
    };
}

static bool has_ext(const fs::path& file, const std::vector<std::string>& exts) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto& e : exts) {
        if (ext == e) return true;
    }
    return false;
}

bool detect_game(const fs::path& folder) {
    if (!fs::is_directory(folder)) return false;
    return fs::exists(folder / "RPG_RT.ldb") || fs::exists(folder / "RPG_RT.lmt");
}

bool is_rm2003(const fs::path& folder) {
    return fs::is_directory(folder / "Battle2");
}

bool backup_game(const fs::path& folder, LogFn log) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_buf);

    fs::path backup_dir = folder.parent_path() / (folder.filename().string() + "_backup_" + ts);
    log("Creating backup: " + backup_dir.string());

    std::error_code ec;
    fs::copy(folder, backup_dir,
             fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    if (ec) {
        log("Backup error: " + ec.message());
        return false;
    }
    log("Backup complete.");
    return true;
}

bool restore_backup(const fs::path& game_path, LogFn log) {
    std::string game_name = game_path.filename().string();
    fs::path parent = game_path.parent_path();

    // Find the most recent backup folder
    std::string best_name;
    for (auto& entry : fs::directory_iterator(parent)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.find(game_name + "_backup_") == 0) {
            if (name > best_name) best_name = name; // lexicographic compare works for timestamps
        }
    }

    if (best_name.empty()) {
        log("No backup found for \"" + game_name + "\".");
        return false;
    }

    fs::path backup_dir = parent / best_name;
    log("Restoring from: " + backup_dir.string());

    // Copy backup contents back, overwriting existing files
    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(backup_dir)) {
        fs::path rel = fs::relative(entry.path(), backup_dir, ec);
        fs::path dest = game_path / rel;
        if (entry.is_directory()) {
            fs::create_directories(dest, ec);
        } else if (entry.is_regular_file()) {
            fs::create_directories(dest.parent_path(), ec);
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                log("  Restore error: " + dest.string() + " — " + ec.message());
                ec.clear();
            }
        }
    }

    log("Restore complete.");
    return true;
}

ShuffleResult shuffle_category(const fs::path& game_path,
                               const AssetCategory& cat, std::mt19937& rng) {
    ShuffleResult result;
    result.category = cat.name;

    fs::path dir = game_path / cat.folder;
    if (!fs::is_directory(dir)) {
        result.message = cat.folder + "/ not found — skipped.";
        return result;
    }

    std::vector<fs::path> files;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && has_ext(entry.path(), cat.extensions)) {
            files.push_back(entry.path());
        }
    }

    result.files_found = static_cast<int>(files.size());
    if (files.size() < 2) {
        result.message = cat.folder + ": " + std::to_string(files.size()) + " file(s) — need >=2.";
        return result;
    }

    std::vector<fs::path> shuffled = files;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    // Force at least one difference
    bool same = true;
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i] != shuffled[i]) { same = false; break; }
    }
    if (same && files.size() >= 2) std::swap(shuffled[0], shuffled[1]);

    // Rename originals → tmp
    std::vector<fs::path> tmp_names;
    for (size_t i = 0; i < files.size(); ++i) {
        fs::path tmp = files[i].parent_path() /
                       ("__hate2k_tmp_" + std::to_string(i) + files[i].extension().string());
        std::error_code ec;
        fs::rename(files[i], tmp, ec);
        if (ec) {
            result.message = "Rename error: " + ec.message();
            for (size_t j = 0; j < i; ++j) fs::rename(tmp_names[j], files[j]);
            return result;
        }
        tmp_names.push_back(tmp);
    }

    // Rename tmp → shuffled destinations
    int count = 0;
    for (size_t i = 0; i < shuffled.size(); ++i) {
        auto it = std::find(files.begin(), files.end(), shuffled[i]);
        size_t src_idx = static_cast<size_t>(std::distance(files.begin(), it));

        std::error_code ec;
        fs::rename(tmp_names[src_idx], files[i], ec);
        if (ec) {
            result.message = "Rename error (step 2): " + ec.message();
            return result;
        }
        if (files[i] != shuffled[i]) ++count;
    }

    result.files_shuffled = count;
    result.message = cat.folder + ": shuffled " + std::to_string(count) + "/" +
                     std::to_string(files.size()) + " files.";
    return result;
}

std::vector<ShuffleResult> shuffle_all(const fs::path& game_path,
                                       const std::vector<bool>& category_enabled,
                                       bool do_backup, LogFn log) {
    std::vector<ShuffleResult> results;
    auto cats = all_categories();

    if (do_backup) {
        if (!backup_game(game_path, log))
            log("WARNING: backup failed — continuing anyway.");
    }

    std::random_device rd;
    std::mt19937 rng(rd());

    for (size_t i = 0; i < cats.size() && i < category_enabled.size(); ++i) {
        if (!category_enabled[i]) continue;
        log("Shuffling " + cats[i].name + "...");
        auto r = shuffle_category(game_path, cats[i], rng);
        results.push_back(r);
        log("  " + r.message);
    }

    log("Done — " + std::to_string(results.size()) + " categories processed.");
    return results;
}

} // namespace hate2k
