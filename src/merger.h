#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace hate2k {

using LogFn = std::function<void(const std::string&)>;

struct MergeResult {
    int games_merged = 0;
    int maps_merged = 0;
    int db_entries_merged = 0;
    std::string message;
};

bool validate_merge_folders(const std::vector<std::filesystem::path>& games,
                            LogFn log);

MergeResult merge_games(const std::vector<std::filesystem::path>& games,
                        bool merge_assets, LogFn log);

} // namespace hate2k
