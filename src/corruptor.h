#pragma once

#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace hate2k {

struct AssetCategory {
    std::string name;
    std::string folder;
    std::vector<std::string> extensions;
};

struct ShuffleResult {
    std::string category;
    int files_found = 0;
    int files_shuffled = 0;
    std::string message;
};

using LogFn = std::function<void(const std::string&)>;

std::vector<AssetCategory> all_categories();
bool detect_game(const std::filesystem::path& folder);
bool is_rm2003(const std::filesystem::path& folder);
bool backup_game(const std::filesystem::path& folder, LogFn log);
bool restore_backup(const std::filesystem::path& game_path, LogFn log);
ShuffleResult shuffle_category(const std::filesystem::path& game_path,
                               const AssetCategory& cat, std::mt19937& rng);
std::vector<ShuffleResult> shuffle_all(const std::filesystem::path& game_path,
                                       const std::vector<bool>& category_enabled,
                                       bool do_backup, LogFn log);

} // namespace hate2k
