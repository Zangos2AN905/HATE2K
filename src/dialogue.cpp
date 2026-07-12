#include "dialogue.h"

#include <lcf/rpg/map.h>
#include <lcf/rpg/event.h>
#include <lcf/rpg/eventpage.h>
#include <lcf/rpg/eventcommand.h>
#include <lcf/rpg/database.h>
#include <lcf/rpg/actor.h>
#include <lcf/lmu/reader.h>
#include <lcf/ldb/reader.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;
using EC = lcf::rpg::EventCommand::Code;

namespace hate2k {

// ── Helpers ─────────────────────────────────────────────────────────────

struct TextRef {
    lcf::DBString* str;
    int map_idx;
    int event_idx;
    int page_idx;
    int cmd_idx;
};

static std::vector<TextRef> collect_messages(lcf::rpg::Map& map, int map_idx) {
    std::vector<TextRef> refs;
    for (int ei = 0; ei < static_cast<int>(map.events.size()); ++ei) {
        auto& ev = map.events[ei];
        for (int pi = 0; pi < static_cast<int>(ev.pages.size()); ++pi) {
            auto& page = ev.pages[pi];
            for (int ci = 0; ci < static_cast<int>(page.event_commands.size()); ++ci) {
                auto& cmd = page.event_commands[ci];
                auto code = static_cast<EC>(cmd.code);
                if (code == EC::ShowMessage || code == EC::ShowMessage_2 ||
                    code == EC::ShowChoice || code == EC::ShowChoiceOption ||
                    code == EC::Comment || code == EC::Comment_2) {
                    if (!cmd.string.empty()) {
                        refs.push_back({&cmd.string, map_idx, ei, pi, ci});
                    }
                }
            }
        }
    }
    return refs;
}

static std::vector<TextRef> collect_words_refs(lcf::rpg::Map& map, int map_idx) {
    // Only collect ShowMessage / ShowMessage_2 for word shuffling
    std::vector<TextRef> refs;
    for (int ei = 0; ei < static_cast<int>(map.events.size()); ++ei) {
        auto& ev = map.events[ei];
        for (int pi = 0; pi < static_cast<int>(ev.pages.size()); ++pi) {
            auto& page = ev.pages[pi];
            for (int ci = 0; ci < static_cast<int>(page.event_commands.size()); ++ci) {
                auto& cmd = page.event_commands[ci];
                auto code = static_cast<EC>(cmd.code);
                if ((code == EC::ShowMessage || code == EC::ShowMessage_2) &&
                    !cmd.string.empty()) {
                    refs.push_back({&cmd.string, map_idx, ei, pi, ci});
                }
            }
        }
    }
    return refs;
}

static std::string dbstr_to_std(const lcf::DBString& s) {
    return std::string(s.data(), s.size());
}

static lcf::DBString std_to_dbstr(const std::string& s) {
    return lcf::DBString(s);
}

static std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> words;
    std::string current;
    for (char c : s) {
        if (c == ' ') {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
            words.push_back(" ");
        } else {
            current += c;
        }
    }
    if (!current.empty()) words.push_back(current);
    return words;
}

static std::string join_words(const std::vector<std::string>& words) {
    std::string result;
    for (auto& w : words) result += w;
    return result;
}

static std::vector<fs::path> find_lmu_files(const fs::path& game_path) {
    std::vector<fs::path> files;
    for (auto& entry : fs::directory_iterator(game_path)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".lmu") {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

struct LoadedMap {
    fs::path path;
    std::unique_ptr<lcf::rpg::Map> map;
};

static std::vector<LoadedMap> load_all_maps(const fs::path& game_path, LogFn log) {
    auto paths = find_lmu_files(game_path);
    std::vector<LoadedMap> maps;
    for (auto& p : paths) {
        auto m = lcf::LMU_Reader::Load(p.string());
        if (m) {
            maps.push_back({p, std::move(m)});
        } else {
            log("  Warning: failed to load " + p.filename().string());
        }
    }
    log("  Loaded " + std::to_string(maps.size()) + " map(s).");
    return maps;
}

static bool save_map(LoadedMap& lm, bool is_rm2003) {
    auto engine = is_rm2003 ? lcf::EngineVersion::e2k3 : lcf::EngineVersion::e2k;
    return lcf::LMU_Reader::Save(lm.path.string(), *lm.map, engine);
}

// ── Engine 1: Shuffle across maps ───────────────────────────────────────

DialogueShuffleResult shuffle_dialogue_across_maps(
    const fs::path& game_path, std::mt19937& rng, LogFn log)
{
    DialogueShuffleResult result;
    auto maps = load_all_maps(game_path, log);
    if (maps.empty()) { result.message = "No maps loaded."; return result; }

    bool is_rm2003 = fs::is_directory(game_path / "Battle2");

    // Collect all text references
    std::vector<TextRef> all_refs;
    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        auto refs = collect_messages(*maps[i].map, i);
        all_refs.insert(all_refs.end(), refs.begin(), refs.end());
    }

    result.messages_found = static_cast<int>(all_refs.size());
    if (all_refs.size() < 2) {
        result.message = "Only " + std::to_string(all_refs.size()) + " message(s) found — need >=2.";
        return result;
    }

    // Extract strings and shuffle
    std::vector<std::string> strings;
    for (auto& ref : all_refs) strings.push_back(dbstr_to_std(*ref.str));
    std::shuffle(strings.begin(), strings.end(), rng);

    // Write back
    for (size_t i = 0; i < all_refs.size(); ++i) {
        *all_refs[i].str = std_to_dbstr(strings[i]);
    }

    // Save all maps
    int saved = 0;
    for (auto& lm : maps) {
        if (save_map(lm, is_rm2003)) ++saved;
        else log("  Warning: failed to save " + lm.path.filename().string());
    }

    result.maps_processed = saved;
    result.messages_shuffled = static_cast<int>(all_refs.size());
    result.message = "Shuffled " + std::to_string(all_refs.size()) + " messages across " +
                     std::to_string(saved) + " maps.";
    return result;
}

// ── Engine 2: Shuffle within maps ───────────────────────────────────────

DialogueShuffleResult shuffle_dialogue_within_maps(
    const fs::path& game_path, std::mt19937& rng, LogFn log)
{
    DialogueShuffleResult result;
    auto maps = load_all_maps(game_path, log);
    if (maps.empty()) { result.message = "No maps loaded."; return result; }

    bool is_rm2003 = fs::is_directory(game_path / "Battle2");
    int total_shuffled = 0;

    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        auto refs = collect_messages(*maps[i].map, i);
        if (refs.size() < 2) continue;

        std::vector<std::string> strings;
        for (auto& ref : refs) strings.push_back(dbstr_to_std(*ref.str));
        std::shuffle(strings.begin(), strings.end(), rng);

        for (size_t j = 0; j < refs.size(); ++j) {
            *refs[j].str = std_to_dbstr(strings[j]);
        }
        total_shuffled += static_cast<int>(refs.size());
    }

    int saved = 0;
    for (auto& lm : maps) {
        if (save_map(lm, is_rm2003)) ++saved;
    }

    result.maps_processed = saved;
    result.messages_found = total_shuffled;
    result.messages_shuffled = total_shuffled;
    result.message = "Shuffled " + std::to_string(total_shuffled) + " messages within " +
                     std::to_string(saved) + " maps.";
    return result;
}

// ── Engine 3: Shuffle words ─────────────────────────────────────────────

DialogueShuffleResult shuffle_words(
    const fs::path& game_path, std::mt19937& rng, LogFn log)
{
    DialogueShuffleResult result;
    auto maps = load_all_maps(game_path, log);
    if (maps.empty()) { result.message = "No maps loaded."; return result; }

    bool is_rm2003 = fs::is_directory(game_path / "Battle2");

    // Collect all words from all messages
    std::vector<TextRef> all_refs;
    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        auto refs = collect_words_refs(*maps[i].map, i);
        all_refs.insert(all_refs.end(), refs.begin(), refs.end());
    }

    result.messages_found = static_cast<int>(all_refs.size());
    if (all_refs.empty()) {
        result.message = "No messages found.";
        return result;
    }

    // Split each message into words, collect all word tokens
    std::vector<std::vector<std::string>> split_msgs;
    std::vector<std::string> all_words;
    for (auto& ref : all_refs) {
        auto words = split_words(dbstr_to_std(*ref.str));
        split_msgs.push_back(words);
        for (auto& w : words) {
            if (w != " ") all_words.push_back(w);
        }
    }

    if (all_words.size() < 2) {
        result.message = "Not enough words to shuffle.";
        return result;
    }

    // Shuffle the word pool
    std::shuffle(all_words.begin(), all_words.end(), rng);

    // Reassign words back to messages (preserving spaces)
    size_t word_idx = 0;
    for (size_t i = 0; i < split_msgs.size(); ++i) {
        for (auto& token : split_msgs[i]) {
            if (token != " " && word_idx < all_words.size()) {
                token = all_words[word_idx++];
            }
        }
        *all_refs[i].str = std_to_dbstr(join_words(split_msgs[i]));
    }

    int saved = 0;
    for (auto& lm : maps) {
        if (save_map(lm, is_rm2003)) ++saved;
    }

    result.maps_processed = saved;
    result.messages_shuffled = static_cast<int>(all_refs.size());
    result.message = "Shuffled " + std::to_string(word_idx) + " words across " +
                     std::to_string(all_refs.size()) + " messages in " +
                     std::to_string(saved) + " maps.";
    return result;
}

// ── Engine 4: Shuffle letter case ───────────────────────────────────────

DialogueShuffleResult shuffle_case(
    const fs::path& game_path, std::mt19937& rng, LogFn log)
{
    DialogueShuffleResult result;
    auto maps = load_all_maps(game_path, log);
    if (maps.empty()) { result.message = "No maps loaded."; return result; }

    bool is_rm2003 = fs::is_directory(game_path / "Battle2");

    int total = 0;
    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        auto refs = collect_messages(*maps[i].map, i);
        for (auto& ref : refs) {
            std::string s = dbstr_to_std(*ref.str);
            bool changed = false;
            for (auto& c : s) {
                if (std::isalpha(static_cast<unsigned char>(c))) {
                    if (std::bernoulli_distribution(0.5)(rng)) {
                        c = static_cast<char>(
                            std::isupper(static_cast<unsigned char>(c))
                                ? std::tolower(static_cast<unsigned char>(c))
                                : std::toupper(static_cast<unsigned char>(c)));
                        changed = true;
                    }
                }
            }
            if (changed) {
                *ref.str = std_to_dbstr(s);
                ++total;
            }
        }
    }

    int saved = 0;
    for (auto& lm : maps) {
        if (save_map(lm, is_rm2003)) ++saved;
    }

    result.maps_processed = saved;
    result.messages_shuffled = total;
    result.message = "Randomised case in " + std::to_string(total) + " messages across " +
                     std::to_string(saved) + " maps.";
    return result;
}

// ── Engine 5: Markov chain nonsense ─────────────────────────────────────

DialogueShuffleResult markov_nonsense(
    const fs::path& game_path, std::mt19937& rng, LogFn log)
{
    DialogueShuffleResult result;
    auto maps = load_all_maps(game_path, log);
    if (maps.empty()) { result.message = "No maps loaded."; return result; }

    bool is_rm2003 = fs::is_directory(game_path / "Battle2");

    // Phase 1: Build bigram model from all dialogue
    std::vector<std::string> corpus_words;
    std::unordered_map<std::string, std::vector<std::string>> bigrams;
    int source_count = 0;

    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        auto refs = collect_words_refs(*maps[i].map, i);
        for (auto& ref : refs) {
            std::string s = dbstr_to_std(*ref.str);
            if (s.empty()) continue;
            ++source_count;

            // Tokenize
            std::istringstream iss(s);
            std::string word;
            std::vector<std::string> words;
            while (iss >> word) words.push_back(word);

            for (size_t j = 0; j + 1 < words.size(); ++j) {
                bigrams[words[j]].push_back(words[j + 1]);
            }
            if (!words.empty()) {
                corpus_words.push_back(words[0]); // sentence starters
                bigrams[""].push_back(words[0]);
            }
        }
    }

    if (corpus_words.empty() || bigrams.empty()) {
        result.message = "Not enough dialogue for Markov generation.";
        return result;
    }

    log("  Built bigram model from " + std::to_string(source_count) + " messages, " +
        std::to_string(bigrams.size()) + " unique prefixes.");

    // Phase 2: Generate nonsense and inject
    auto generate_sentence = [&](int target_len) -> std::string {
        std::string current = corpus_words[std::uniform_int_distribution<size_t>(
            0, corpus_words.size() - 1)(rng)];
        std::string sentence = current;

        for (int j = 0; j < target_len; ++j) {
            auto it = bigrams.find(current);
            if (it == bigrams.end() || it->second.empty()) break;
            auto& nexts = it->second;
            current = nexts[std::uniform_int_distribution<size_t>(
                0, nexts.size() - 1)(rng)];
            sentence += " " + current;
        }
        return sentence;
    };

    int total = 0;
    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        auto refs = collect_words_refs(*maps[i].map, i);
        for (auto& ref : refs) {
            // Generate a sentence of similar length
            std::string orig = dbstr_to_std(*ref.str);
            int word_count = 1;
            for (char c : orig) if (c == ' ') ++word_count;
            int target_len = std::max(3, word_count + std::uniform_int_distribution<int>(-2, 2)(rng));

            *ref.str = std_to_dbstr(generate_sentence(target_len));
            ++total;
        }
    }

    int saved = 0;
    for (auto& lm : maps) {
        if (save_map(lm, is_rm2003)) ++saved;
    }

    result.maps_processed = saved;
    result.messages_shuffled = total;
    result.message = "Generated " + std::to_string(total) + " Markov nonsense messages across " +
                     std::to_string(saved) + " maps.";
    return result;
}

// ── Map transfer shuffling ──────────────────────────────────────────────

DialogueShuffleResult shuffle_transfers(
    const fs::path& game_path, std::mt19937& rng, LogFn log)
{
    DialogueShuffleResult result;
    auto maps = load_all_maps(game_path, log);
    if (maps.empty()) { result.message = "No maps loaded."; return result; }

    bool is_rm2003 = fs::is_directory(game_path / "Battle2");

    // Collect all Teleport commands (code 10810)
    struct TransferRef {
        int32_t* map_id;
        int map_idx;
    };
    std::vector<TransferRef> transfers;

    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        for (auto& ev : maps[i].map->events) {
            for (auto& page : ev.pages) {
                for (auto& cmd : page.event_commands) {
                    if (static_cast<EC>(cmd.code) == EC::Teleport &&
                        cmd.parameters.size() >= 3) {
                        transfers.push_back({&cmd.parameters[0], i});
                    }
                }
            }
        }
    }

    result.messages_found = static_cast<int>(transfers.size());
    if (transfers.size() < 2) {
        result.message = "Only " + std::to_string(transfers.size()) + " transfer(s) — need >=2.";
        return result;
    }

    // Extract map IDs and shuffle
    std::vector<int32_t> map_ids;
    for (auto& t : transfers) map_ids.push_back(*t.map_id);
    std::shuffle(map_ids.begin(), map_ids.end(), rng);

    // Force at least one difference
    bool same = true;
    for (size_t i = 0; i < transfers.size(); ++i) {
        if (*transfers[i].map_id != map_ids[i]) { same = false; break; }
    }
    if (same && map_ids.size() >= 2) std::swap(map_ids[0], map_ids[1]);

    // Write back
    int count = 0;
    for (size_t i = 0; i < transfers.size(); ++i) {
        if (*transfers[i].map_id != map_ids[i]) ++count;
        *transfers[i].map_id = map_ids[i];
    }

    int saved = 0;
    for (auto& lm : maps) {
        if (save_map(lm, is_rm2003)) ++saved;
    }

    result.maps_processed = saved;
    result.messages_shuffled = count;
    result.message = "Shuffled " + std::to_string(count) + " map transfers across " +
                     std::to_string(saved) + " maps.";
    return result;
}

// ── Player character shuffling ──────────────────────────────────────────

DialogueShuffleResult shuffle_characters(
    const fs::path& game_path, std::mt19937& rng, LogFn log)
{
    DialogueShuffleResult result;

    fs::path ldb_path = game_path / "RPG_RT.ldb";
    if (!fs::exists(ldb_path)) {
        result.message = "RPG_RT.ldb not found.";
        return result;
    }

    auto db = lcf::LDB_Reader::Load(ldb_path.string());
    if (!db) {
        result.message = "Failed to load RPG_RT.ldb.";
        return result;
    }

    // Collect actors with names (skip empty slots)
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(db->actors.size()); ++i) {
        if (!db->actors[i].name.empty()) {
            indices.push_back(i);
        }
    }

    result.messages_found = static_cast<int>(indices.size());
    if (indices.size() < 2) {
        result.message = "Only " + std::to_string(indices.size()) + " actor(s) — need >=2.";
        return result;
    }

    // Shuffle names, titles, character sprites, and face graphics
    std::vector<std::string> names, titles, char_names, face_names;
    std::vector<int32_t> char_indices, face_indices;

    for (int i : indices) {
        auto& a = db->actors[i];
        names.push_back(std::string(a.name.data(), a.name.size()));
        titles.push_back(std::string(a.title.data(), a.title.size()));
        char_names.push_back(std::string(a.character_name.data(), a.character_name.size()));
        char_indices.push_back(a.character_index);
        face_names.push_back(std::string(a.face_name.data(), a.face_name.size()));
        face_indices.push_back(a.face_index);
    }

    std::vector<int> order(indices.size());
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);

    // Force at least one difference
    bool same = true;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] != static_cast<int>(i)) { same = false; break; }
    }
    if (same && order.size() >= 2) std::swap(order[0], order[1]);

    int count = 0;
    for (size_t i = 0; i < indices.size(); ++i) {
        int src = order[i];
        int dst = static_cast<int>(i);
        if (src == dst) continue;

        auto& a = db->actors[indices[dst]];
        a.name = lcf::DBString(names[src]);
        a.title = lcf::DBString(titles[src]);
        a.character_name = lcf::DBString(char_names[src]);
        a.character_index = char_indices[src];
        a.face_name = lcf::DBString(face_names[src]);
        a.face_index = face_indices[src];
        ++count;
    }

    bool is_rm2003 = fs::is_directory(game_path / "Battle2");
    lcf::LDB_Reader::Save(ldb_path.string(), *db);

    result.messages_shuffled = count;
    result.message = "Shuffled " + std::to_string(count) + " player characters.";
    return result;
}

} // namespace hate2k
