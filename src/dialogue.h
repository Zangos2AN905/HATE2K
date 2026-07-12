#pragma once

#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace hate2k {

using LogFn = std::function<void(const std::string&)>;

struct DialogueShuffleResult {
    int maps_processed = 0;
    int messages_found = 0;
    int messages_shuffled = 0;
    std::string message;
};

// ── Dialogue engines ────────────────────────────────────────────────────
DialogueShuffleResult shuffle_dialogue_across_maps(
    const std::filesystem::path& game_path, std::mt19937& rng, LogFn log);
DialogueShuffleResult shuffle_dialogue_within_maps(
    const std::filesystem::path& game_path, std::mt19937& rng, LogFn log);
DialogueShuffleResult shuffle_words(
    const std::filesystem::path& game_path, std::mt19937& rng, LogFn log);
DialogueShuffleResult shuffle_case(
    const std::filesystem::path& game_path, std::mt19937& rng, LogFn log);
DialogueShuffleResult markov_nonsense(
    const std::filesystem::path& game_path, std::mt19937& rng, LogFn log);

// ── Map transfer shuffling ──────────────────────────────────────────────
DialogueShuffleResult shuffle_transfers(
    const std::filesystem::path& game_path, std::mt19937& rng, LogFn log);

// ── Player character shuffling ──────────────────────────────────────────
DialogueShuffleResult shuffle_characters(
    const std::filesystem::path& game_path, std::mt19937& rng, LogFn log);

} // namespace hate2k
