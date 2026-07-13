#include "merger.h"

#include <lcf/rpg/database.h>
#include <lcf/rpg/treemap.h>
#include <lcf/rpg/map.h>
#include <lcf/rpg/event.h>
#include <lcf/rpg/eventpage.h>
#include <lcf/rpg/eventcommand.h>
#include <lcf/rpg/eventpagecondition.h>
#include <lcf/rpg/mapinfo.h>
#include <lcf/rpg/commonevent.h>
#include <lcf/rpg/actor.h>
#include <lcf/rpg/skill.h>
#include <lcf/rpg/item.h>
#include <lcf/rpg/enemy.h>
#include <lcf/rpg/enemyaction.h>
#include <lcf/rpg/troop.h>
#include <lcf/rpg/troopmember.h>
#include <lcf/rpg/encounter.h>
#include <lcf/rpg/learning.h>
#include <lcf/rpg/equipment.h>
#include <lcf/rpg/moveroute.h>
#include <lcf/rpg/movecommand.h>
#include <lcf/rpg/animation.h>
#include <lcf/rpg/chipset.h>
#include <lcf/rpg/battleranimation.h>
#include <lcf/rpg/battleranimationpose.h>
#include <lcf/rpg/battleranimationweapon.h>
#include <lcf/ldb/reader.h>
#include <lcf/lmt/reader.h>
#include <lcf/lmu/reader.h>
#include <lcf/reader_util.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;
using EC = lcf::rpg::EventCommand::Code;

namespace hate2k {

// ============================================================================
// Encoding
// ============================================================================

static std::string read_ini_encoding(const fs::path& game_path) {
    fs::path ini_path = game_path / "RPG_RT.ini";
    if (fs::exists(ini_path)) {
        std::string enc = lcf::ReaderUtil::GetEncoding(ini_path.string());
        if (!enc.empty()) return enc;
    }
    return {};
}

static std::string resolve_encoding(const std::vector<fs::path>& games, LogFn log) {
    std::string canonical;
    for (auto& g : games) {
        std::string enc = read_ini_encoding(g);
        if (!enc.empty()) { canonical = enc; break; }
    }
    if (!canonical.empty()) {
        log("  Canonical encoding: " + canonical);
        for (size_t i = 0; i < games.size(); ++i) {
            std::string enc = read_ini_encoding(games[i]);
            if (!enc.empty() && enc != canonical) {
                log("  WARNING: " + games[i].filename().string() +
                    " has different encoding (" + enc + ") — will use " + canonical);
            }
        }
        return canonical;
    }
    auto db = lcf::LDB_Reader::Load((games[0] / "RPG_RT.ldb").string(), "");
    if (db) {
        auto detected = lcf::ReaderUtil::DetectEncoding(*db);
        if (!detected.empty()) {
            log("  Auto-detected encoding: " + detected);
            return detected;
        }
    }
    log("  No encoding — strings will be preserved as-is.");
    return {};
}

// ============================================================================
// LMU file scanning
// ============================================================================

static std::string to_lower_ext(const fs::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

static int extract_trailing_digits(const std::string& s) {
    std::string digits;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        if (std::isdigit(static_cast<unsigned char>(s[i])))
            digits = s[i] + digits;
        else
            break;
    }
    if (digits.empty()) return -1;
    try { return std::stoi(digits); } catch (...) { return -1; }
}

static std::unordered_map<int, fs::path> scan_lmu_files(const fs::path& dir) {
    std::unordered_map<int, fs::path> result;
    if (!fs::is_directory(dir)) return result;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (to_lower_ext(entry.path()) != ".lmu") continue;
        int id = extract_trailing_digits(entry.path().stem().string());
        if (id > 0) result[id] = entry.path();
    }
    return result;
}

static std::set<int> disk_map_ids(const fs::path& dir) {
    auto files = scan_lmu_files(dir);
    std::set<int> ids;
    for (auto& [id, path] : files) ids.insert(id);
    return ids;
}

struct LmuNaming {
    std::string prefix = "RPG_RT";
    int num_digits = 4;
};

static LmuNaming detect_lmu_naming(const fs::path& dir) {
    LmuNaming naming;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (to_lower_ext(entry.path()) != ".lmu") continue;
        std::string stem = entry.path().stem().string();
        int digit_start = (int)stem.size();
        for (int i = (int)stem.size() - 1; i >= 0; --i) {
            if (std::isdigit(static_cast<unsigned char>(stem[i])))
                digit_start = i;
            else
                break;
        }
        naming.prefix = stem.substr(0, digit_start);
        naming.num_digits = std::max(4, (int)stem.size() - digit_start);
        return naming;
    }
    return naming;
}

static std::string format_lmu_name(const LmuNaming& naming, int id) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s%0*d", naming.prefix.c_str(), naming.num_digits, id);
    return std::string(buf) + ".lmu";
}

// ============================================================================
// ID offset structure
// ============================================================================

struct IdOffsets {
    int actors = 0;
    int skills = 0;
    int items = 0;
    int enemies = 0;
    int troops = 0;
    int terrains = 0;
    int attributes = 0;
    int states = 0;
    int animations = 0;
    int chipsets = 0;
    int switches = 0;
    int variables = 0;
    int commonevents = 0;
    int classes = 0;
    int battleranimations = 0;
    std::unordered_map<int, int> map_remap;
};

static int count_named(const auto& vec) {
    int count = 0;
    for (auto& e : vec) { if (!e.name.empty()) ++count; }
    return count;
}

static int safe_max(int a, int b) { return a > b ? a : b; }

static IdOffsets calculate_offsets(const lcf::rpg::Database& db,
                                   std::unordered_map<int, int> map_remap) {
    IdOffsets off;
    off.actors = safe_max((int)db.actors.size(), count_named(db.actors));
    off.skills = safe_max((int)db.skills.size(), count_named(db.skills));
    off.items = safe_max((int)db.items.size(), count_named(db.items));
    off.enemies = safe_max((int)db.enemies.size(), count_named(db.enemies));
    off.troops = safe_max((int)db.troops.size(), count_named(db.troops));
    off.terrains = safe_max((int)db.terrains.size(), count_named(db.terrains));
    off.attributes = safe_max((int)db.attributes.size(), count_named(db.attributes));
    off.states = safe_max((int)db.states.size(), count_named(db.states));
    off.animations = safe_max((int)db.animations.size(), count_named(db.animations));
    off.chipsets = safe_max((int)db.chipsets.size(), count_named(db.chipsets));
    off.switches = safe_max((int)db.switches.size(), count_named(db.switches));
    off.variables = safe_max((int)db.variables.size(), count_named(db.variables));
    off.commonevents = safe_max((int)db.commonevents.size(), count_named(db.commonevents));
    off.classes = safe_max((int)db.classes.size(), count_named(db.classes));
    off.battleranimations = safe_max((int)db.battleranimations.size(), count_named(db.battleranimations));
    off.map_remap = std::move(map_remap);
    return off;
}

// ============================================================================
// Map remap builder
// ============================================================================

static std::unordered_map<int, int> build_map_remap(
    const lcf::rpg::TreeMap& existing_tree,
    const fs::path& game2_path, LogFn log)
{
    std::set<int> used;
    for (auto& mi : existing_tree.maps) {
        if (mi.ID > 0) used.insert(mi.ID);
    }
    auto game2_ids_set = disk_map_ids(game2_path);
    std::vector<int> game2_ids(game2_ids_set.begin(), game2_ids_set.end());
    std::sort(game2_ids.begin(), game2_ids.end());

    log("  Existing maps: " + std::to_string(used.size()) +
        ", Game 2 maps: " + std::to_string(game2_ids.size()) + " (from disk)");

    std::unordered_map<int, int> remap;
    int next = 1;
    for (int old_id : game2_ids) {
        while (next <= 9999 && used.count(next)) ++next;
        if (next > 9999) { log("ERROR: Exceeded 9999 map limit."); break; }
        remap[old_id] = next;
        used.insert(next);
        ++next;
    }
    log("  Remapped " + std::to_string(remap.size()) + " maps.");
    return remap;
}

// ============================================================================
// Event command remapping
// ============================================================================

static int remap_map_id(int id, const std::unordered_map<int, int>& mr) {
    auto it = mr.find(id);
    return it != mr.end() ? it->second : id;
}

static void remap_event_command(lcf::rpg::EventCommand& cmd, const IdOffsets& off) {
    auto code = static_cast<EC>(cmd.code);
    auto& p = cmd.parameters;
    switch (code) {
    case EC::Teleport:
        if (p.size() > 0) p[0] = remap_map_id(p[0], off.map_remap);
        break;
    case EC::EscapeTarget:
        if (p.size() > 0) p[0] = remap_map_id(p[0], off.map_remap);
        break;
    case EC::TeleportTargets: {
        int count = (int)p.size() > 0 ? p[0] : 0;
        for (int i = 0; i < count; ++i) {
            int base = 1 + i * 5;
            if (base < (int)p.size()) p[base] = remap_map_id(p[base], off.map_remap);
            if (base + 4 < (int)p.size()) p[base + 4] += off.switches;
        }
        break;
    }
    case EC::SetVehicleLocation:
        if (p.size() > 2 && p[1] == 1) p[2] = remap_map_id(p[2], off.map_remap);
        break;
    case EC::ControlSwitches:
        if (p.size() > 1) p[1] += off.switches;
        if (p.size() > 2 && p[0] == 1) p[2] += off.switches;
        break;
    case EC::ControlVars:
        if (p.size() > 1) p[1] += off.variables;
        if (p.size() > 2 && p[0] == 1) p[2] += off.variables;
        if (p.size() > 4 && p[4] == 1 && p.size() > 5) p[5] += off.variables;
        break;
    case EC::ChangeExp: case EC::ChangeLevel: case EC::ChangeParameters:
    case EC::ChangeHP: case EC::ChangeSP: case EC::FullHeal: case EC::SimulatedAttack:
        if (p.size() > 1 && p[0] == 0) p[1] += off.actors;
        break;
    case EC::ChangeSkills:
        if (p.size() > 1 && p[0] == 0) p[1] += off.actors;
        if (p.size() > 4) p[4] += off.skills;
        break;
    case EC::ChangeEquipment:
        if (p.size() > 1 && p[0] == 0) p[1] += off.actors;
        if (p.size() > 4) p[4] += off.items;
        break;
    case EC::ChangeCondition:
        if (p.size() > 1 && p[0] == 0) p[1] += off.actors;
        if (p.size() > 3) p[3] += off.states;
        if (p.size() > 4) p[4] += off.states;
        break;
    case EC::ChangeHeroName: case EC::ChangeHeroTitle: case EC::ChangeSpriteAssociation:
    case EC::ChangeActorFace: case EC::EnterHeroName:
        if (p.size() > 0) p[0] += off.actors;
        break;
    case EC::ChangePartyMembers:
        if (p.size() > 1) p[1] += off.actors;
        break;
    case EC::ChangeItems:
        if (p.size() > 2) p[2] += off.items;
        break;
    case EC::OpenShop:
        for (size_t i = 1; i + 1 < p.size(); i += 3) p[i] += off.items;
        break;
    case EC::EnemyEncounter:
        // p[0]=scope(0=variable,1=fixed,2=random), p[1]=troop_id or variable_id
        if (p.size() > 1 && p[0] == 1) p[1] += off.troops;
        if (p.size() > 1 && p[0] == 0) p[1] += off.variables;
        if (p.size() > 4) p[4] += off.switches; // escape switch
        if (p.size() > 5) p[5] += off.switches; // defeat switch
        break;
    case EC::ShowBattleAnimation: case EC::ShowBattleAnimation_B:
        if (p.size() > 0) p[0] += off.animations;
        break;
    case EC::ChangeMapTileset:
        if (p.size() > 0) p[0] += off.chipsets;
        break;
    case EC::CallCommonEvent:
        if (p.size() > 0) p[0] += off.commonevents;
        break;
    case EC::CallEvent:
        if (p.size() > 1 && p[0] == 2) p[1] += off.commonevents;
        break;
    case EC::ChangeTeleportAccess: case EC::ChangeEscapeAccess:
    case EC::ChangeSaveAccess: case EC::ChangeMainMenuAccess:
        if (p.size() > 1 && p[0] == 1) p[1] += off.switches;
        break;
    case EC::ChangeClass:
        if (p.size() > 1 && p[0] == 0) p[1] += off.actors;
        if (p.size() > 3) p[3] += off.classes;
        break;
    case EC::ChangeBattleCommands:
        if (p.size() > 1 && p[0] == 0) p[1] += off.actors;
        break;
    case EC::ConditionalBranch:
        if (p.size() > 1) {
            switch (p[0]) {
            case 0: if (p.size() > 1) p[1] += off.switches; break;
            case 1:
                if (p.size() > 1) p[1] += off.variables;
                if (p.size() > 3 && p[2] == 1) p[3] += off.variables;
                break;
            case 3: case 5: case 8: if (p.size() > 1) p[1] += off.actors; break;
            case 4: if (p.size() > 1) p[1] += off.items; break;
            case 9: if (p.size() > 1) p[1] += off.actors; if (p.size() > 2) p[2] += off.skills; break;
            case 10: if (p.size() > 1) p[1] += off.actors; if (p.size() > 2) p[2] += off.items; break;
            case 11: if (p.size() > 1) p[1] += off.actors; if (p.size() > 2) p[2] += off.items; break;
            case 12: if (p.size() > 1) p[1] += off.actors; if (p.size() > 2) p[2] += off.states; break;
            default: break;
            }
        }
        break;
    case EC::ConditionalBranch_B:
        if (p.size() > 1 && p[0] == 5) p[1] += off.actors;
        break;
    default: break;
    }
}

static void remap_page_condition(lcf::rpg::EventPageCondition& cond, const IdOffsets& off) {
    if (cond.flags.switch_a) cond.switch_a_id += off.switches;
    if (cond.flags.switch_b) cond.switch_b_id += off.switches;
    if (cond.flags.variable) cond.variable_id += off.variables;
    if (cond.flags.item) cond.item_id += off.items;
    if (cond.flags.actor) cond.actor_id += off.actors;
}

static void remap_move_route(lcf::rpg::MoveRoute& route, const IdOffsets& off) {
    for (auto& cmd : route.move_commands) {
        if (cmd.command_id == 32) cmd.parameter_a += off.switches;
        if (cmd.command_id == 33) cmd.parameter_a += off.switches;
    }
}

static void remap_map_events(lcf::rpg::Map& map, const IdOffsets& off) {
    map.chipset_id += off.chipsets;
    for (auto& ev : map.events) {
        for (auto& page : ev.pages) {
            remap_page_condition(page.condition, off);
            remap_move_route(page.move_route, off);
            for (auto& cmd : page.event_commands) remap_event_command(cmd, off);
        }
    }
}

// ============================================================================
// Database merging
// ============================================================================

template<typename T>
static int append_entities(std::vector<T>& vec1, const std::vector<T>& vec2, int offset) {
    int count = 0;
    int needed = offset + (int)vec2.size();
    if (needed > (int)vec1.size()) vec1.resize(needed);
    for (size_t i = 0; i < vec2.size(); ++i) {
        if (!vec2[i].name.empty()) {
            vec1[offset + i] = vec2[i];
            vec1[offset + i].ID = offset + (int)i + 1;
            ++count;
        }
    }
    return count;
}

static int merge_database(lcf::rpg::Database& db1, const lcf::rpg::Database& db2,
                           const IdOffsets& off, LogFn log) {
    int total = 0;
    total += append_entities(db1.actors, db2.actors, off.actors);
    total += append_entities(db1.skills, db2.skills, off.skills);
    total += append_entities(db1.items, db2.items, off.items);
    total += append_entities(db1.enemies, db2.enemies, off.enemies);
    total += append_entities(db1.troops, db2.troops, off.troops);
    total += append_entities(db1.terrains, db2.terrains, off.terrains);
    total += append_entities(db1.attributes, db2.attributes, off.attributes);
    total += append_entities(db1.states, db2.states, off.states);
    total += append_entities(db1.animations, db2.animations, off.animations);
    total += append_entities(db1.chipsets, db2.chipsets, off.chipsets);
    total += append_entities(db1.switches, db2.switches, off.switches);
    total += append_entities(db1.variables, db2.variables, off.variables);
    total += append_entities(db1.commonevents, db2.commonevents, off.commonevents);
    total += append_entities(db1.classes, db2.classes, off.classes);
    total += append_entities(db1.battleranimations, db2.battleranimations, off.battleranimations);
    log("  Appended " + std::to_string(total) + " database entries.");
    return total;
}

// ============================================================================
// Rank/bitarray extension
// ============================================================================

static void extend_ranks(std::vector<uint8_t>& ranks, int new_size) {
    auto old_size = (int)ranks.size();
    if (new_size <= old_size) return;
    std::vector<uint8_t> new_ranks(new_size, 3);
    for (int i = 0; i < old_size; ++i) new_ranks[i] = ranks[i];
    ranks = std::move(new_ranks);
}

static void extend_bitarray(lcf::DBBitArray& bits, int new_size) {
    auto old_size = (int)bits.size();
    if (new_size <= old_size) return;
    lcf::DBBitArray new_bits(new_size, false);
    for (int i = 0; i < old_size; ++i) { if (bits[i]) new_bits.set(i); }
    bits = std::move(new_bits);
}

static void extend_existing_ranks_and_bitarrays(lcf::rpg::Database& db, const IdOffsets& off) {
    int new_states = (int)db.states.size();
    int new_attributes = (int)db.attributes.size();
    int new_terrains = (int)db.terrains.size();
    int new_actors = (int)db.actors.size();
    int new_classes = (int)db.classes.size();

    for (int i = 0; i < off.actors && i < (int)db.actors.size(); ++i) {
        auto& a = db.actors[i];
        extend_ranks(a.state_ranks, new_states);
        extend_ranks(a.attribute_ranks, new_attributes);
        extend_bitarray(a.easyrpg_unarmed_state_set, new_states);
        extend_bitarray(a.easyrpg_unarmed_attribute_set, new_attributes);
    }
    for (int i = 0; i < off.enemies && i < (int)db.enemies.size(); ++i) {
        auto& e = db.enemies[i];
        extend_ranks(e.state_ranks, new_states);
        extend_ranks(e.attribute_ranks, new_attributes);
        extend_bitarray(e.easyrpg_state_set, new_states);
        extend_bitarray(e.easyrpg_attribute_set, new_attributes);
    }
    for (int i = 0; i < off.classes && i < (int)db.classes.size(); ++i) {
        extend_ranks(db.classes[i].state_ranks, new_states);
        extend_ranks(db.classes[i].attribute_ranks, new_attributes);
    }
    for (int i = 0; i < off.skills && i < (int)db.skills.size(); ++i) {
        extend_bitarray(db.skills[i].state_effects, new_states);
        extend_bitarray(db.skills[i].attribute_effects, new_attributes);
    }
    for (int i = 0; i < off.items && i < (int)db.items.size(); ++i) {
        auto& it = db.items[i];
        extend_bitarray(it.actor_set, new_actors);
        extend_bitarray(it.state_set, new_states);
        extend_bitarray(it.attribute_set, new_attributes);
        extend_bitarray(it.class_set, new_classes);
    }
    for (int i = 0; i < off.troops && i < (int)db.troops.size(); ++i) {
        extend_bitarray(db.troops[i].terrain_set, new_terrains);
    }
}

// ============================================================================
// Database cross-reference remapping
// ============================================================================

static void remap_db_references(lcf::rpg::Database& db, const IdOffsets& off, LogFn log) {
    int cur_states = (int)db.states.size();
    int cur_attributes = (int)db.attributes.size();
    int cur_actors = (int)db.actors.size();
    int cur_classes = (int)db.classes.size();
    int cur_terrains = (int)db.terrains.size();

    for (int i = off.actors; i < (int)db.actors.size(); ++i) {
        auto& a = db.actors[i];
        extend_ranks(a.state_ranks, cur_states);
        extend_ranks(a.attribute_ranks, cur_attributes);
        extend_bitarray(a.easyrpg_unarmed_state_set, cur_states);
        extend_bitarray(a.easyrpg_unarmed_attribute_set, cur_attributes);
    }
    for (int i = off.enemies; i < (int)db.enemies.size(); ++i) {
        auto& e = db.enemies[i];
        extend_ranks(e.state_ranks, cur_states);
        extend_ranks(e.attribute_ranks, cur_attributes);
        extend_bitarray(e.easyrpg_state_set, cur_states);
        extend_bitarray(e.easyrpg_attribute_set, cur_attributes);
    }
    for (int i = off.skills; i < (int)db.skills.size(); ++i) {
        extend_bitarray(db.skills[i].state_effects, cur_states);
        extend_bitarray(db.skills[i].attribute_effects, cur_attributes);
    }
    for (int i = off.items; i < (int)db.items.size(); ++i) {
        auto& it = db.items[i];
        extend_bitarray(it.actor_set, cur_actors);
        extend_bitarray(it.state_set, cur_states);
        extend_bitarray(it.attribute_set, cur_attributes);
        extend_bitarray(it.class_set, cur_classes);
    }
    for (int i = off.troops; i < (int)db.troops.size(); ++i) {
        extend_bitarray(db.troops[i].terrain_set, cur_terrains);
    }
    for (int i = off.classes; i < (int)db.classes.size(); ++i) {
        extend_ranks(db.classes[i].state_ranks, cur_states);
        extend_ranks(db.classes[i].attribute_ranks, cur_attributes);
    }

    for (int i = off.actors; i < (int)db.actors.size(); ++i) {
        auto& a = db.actors[i];
        if (a.name.empty()) continue;
        a.unarmed_animation += off.animations;
        a.class_id += off.classes;
        a.battler_animation += off.battleranimations;
        for (auto& l : a.skills) l.skill_id += off.skills;
        a.initial_equipment.weapon_id += off.items;
        a.initial_equipment.shield_id += off.items;
        a.initial_equipment.armor_id += off.items;
        a.initial_equipment.helmet_id += off.items;
        a.initial_equipment.accessory_id += off.items;
    }
    for (int i = off.skills; i < (int)db.skills.size(); ++i) {
        auto& s = db.skills[i]; if (s.name.empty()) continue;
        s.switch_id += off.switches; s.animation_id += off.animations;
    }
    for (int i = off.items; i < (int)db.items.size(); ++i) {
        auto& it = db.items[i]; if (it.name.empty()) continue;
        it.skill_id += off.skills; it.switch_id += off.switches; it.animation_id += off.animations;
    }
    for (int i = off.enemies; i < (int)db.enemies.size(); ++i) {
        auto& e = db.enemies[i]; if (e.name.empty()) continue;
        e.drop_id += off.items;
        for (auto& act : e.actions) {
            act.skill_id += off.skills; act.enemy_id += off.enemies;
            act.switch_id += off.switches; act.switch_on_id += off.switches; act.switch_off_id += off.switches;
        }
    }
    for (int i = off.troops; i < (int)db.troops.size(); ++i) {
        auto& t = db.troops[i]; if (t.name.empty()) continue;
        for (auto& m : t.members) m.enemy_id += off.enemies;
        for (auto& page : t.pages)
            for (auto& cmd : page.event_commands) remap_event_command(cmd, off);
    }
    for (int i = off.commonevents; i < (int)db.commonevents.size(); ++i) {
        auto& ce = db.commonevents[i]; if (ce.name.empty()) continue;
        ce.switch_id += off.switches;
        for (auto& cmd : ce.event_commands) remap_event_command(cmd, off);
    }
    for (int i = off.classes; i < (int)db.classes.size(); ++i) {
        auto& c = db.classes[i]; if (c.name.empty()) continue;
        for (auto& l : c.skills) l.skill_id += off.skills;
    }
    log("  Remapped database cross-references.");
}

// ============================================================================
// Map tree merging
// ============================================================================

static void merge_map_tree(lcf::rpg::TreeMap& tree1,
                            const lcf::rpg::TreeMap& tree2,
                            const fs::path& game2_path,
                            const IdOffsets& off, LogFn log) {
    std::unordered_map<int, const lcf::rpg::MapInfo*> tree2_by_id;
    for (const auto& mi : tree2.maps)
        if (mi.ID > 0) tree2_by_id[mi.ID] = &mi;

    auto disk_ids = disk_map_ids(game2_path);
    int added = 0;

    for (auto& [old_id, new_id] : off.map_remap) {
        auto tree_it = tree2_by_id.find(old_id);
        lcf::rpg::MapInfo new_mi;
        if (tree_it != tree2_by_id.end()) {
            new_mi = *tree_it->second;
            new_mi.ID = new_id;
            if (new_mi.parent_map > 0) {
                auto pit = off.map_remap.find(new_mi.parent_map);
                if (pit != off.map_remap.end()) new_mi.parent_map = pit->second;
            }
            for (auto& enc : new_mi.encounters) enc.troop_id += off.troops;
        } else {
            new_mi.ID = new_id;
            new_mi.name = lcf::DBString("Map" + std::to_string(new_id));
            new_mi.type = lcf::rpg::TreeMap::MapType_map;
            new_mi.parent_map = 0;
        }
        if (disk_ids.count(old_id))
            new_mi.type = lcf::rpg::TreeMap::MapType_map;
        tree1.maps.push_back(new_mi);
        ++added;
    }

    for (int32_t id : tree2.tree_order) {
        if (id <= 0) continue;
        auto it = off.map_remap.find(id);
        if (it != off.map_remap.end()) tree1.tree_order.push_back(it->second);
    }
    std::set<int> tree2_order_set(tree2.tree_order.begin(), tree2.tree_order.end());
    for (auto& [old_id, new_id] : off.map_remap) {
        if (!tree2_order_set.count(old_id)) tree1.tree_order.push_back(new_id);
    }
    log("  Merged map tree: " + std::to_string(added) + " entries.");
}

// ============================================================================
// Asset name remapping
// ============================================================================

static void remap_asset_str(lcf::DBString& name,
                             const std::unordered_map<std::string, std::string>& renames) {
    if (name.empty()) return;
    std::string s(name.data(), name.size());
    auto it = renames.find(s);
    if (it != renames.end()) name = lcf::DBString(it->second);
}

static void remap_map_asset_names(lcf::rpg::Map& map,
                                   const std::unordered_map<std::string, std::string>& renames) {
    if (renames.empty()) return;
    remap_asset_str(map.parallax_name, renames);
    for (auto& ev : map.events) {
        for (auto& page : ev.pages) {
            remap_asset_str(page.character_name, renames);
            for (auto& cmd : page.event_commands) {
                auto code = static_cast<EC>(cmd.code);
                if (code == EC::PlayBGM || code == EC::PlaySound ||
                    code == EC::ShowPicture || code == EC::MovePicture ||
                    code == EC::ChangeSpriteAssociation ||
                    code == EC::ChangeActorFace ||
                    code == EC::ChangeVehicleGraphic ||
                    code == EC::ChangeSystemGraphics ||
                    code == EC::ChangePBG ||
                    code == EC::ChangeBattleBG) {
                    remap_asset_str(cmd.string, renames);
                }
            }
        }
    }
}

static void remap_db_asset_names(lcf::rpg::Database& db, const IdOffsets& off,
                                  const std::unordered_map<std::string, std::string>& renames) {
    if (renames.empty()) return;
    for (int i = off.actors; i < (int)db.actors.size(); ++i) {
        remap_asset_str(db.actors[i].character_name, renames);
        remap_asset_str(db.actors[i].face_name, renames);
    }
    for (int i = off.enemies; i < (int)db.enemies.size(); ++i)
        remap_asset_str(db.enemies[i].battler_name, renames);
    for (int i = off.animations; i < (int)db.animations.size(); ++i)
        remap_asset_str(db.animations[i].animation_name, renames);
    for (int i = off.chipsets; i < (int)db.chipsets.size(); ++i)
        remap_asset_str(db.chipsets[i].chipset_name, renames);
    for (int i = off.battleranimations; i < (int)db.battleranimations.size(); ++i) {
        auto& ba = db.battleranimations[i];
        for (auto& pose : ba.poses) remap_asset_str(pose.battler_name, renames);
        for (auto& w : ba.weapons) remap_asset_str(w.weapon_name, renames);
    }
    // Remap asset filenames in common event commands (ShowPicture, PlayBGM, etc.)
    for (int i = off.commonevents; i < (int)db.commonevents.size(); ++i) {
        for (auto& cmd : db.commonevents[i].event_commands) {
            auto code = static_cast<EC>(cmd.code);
            if (code == EC::PlayBGM || code == EC::PlaySound ||
                code == EC::ShowPicture || code == EC::MovePicture ||
                code == EC::ChangeSpriteAssociation ||
                code == EC::ChangeActorFace ||
                code == EC::ChangeVehicleGraphic ||
                code == EC::ChangeSystemGraphics ||
                code == EC::ChangePBG ||
                code == EC::ChangeBattleBG) {
                remap_asset_str(cmd.string, renames);
            }
        }
    }
}

// ============================================================================
// LMU file copying with remapping
// ============================================================================

static int copy_renumbered_maps(const fs::path& game2_path,
                                 const fs::path& output_path,
                                 const IdOffsets& off,
                                 lcf::EngineVersion engine,
                                 const std::string& encoding,
                                 const std::unordered_map<std::string, std::string>& asset_renames,
                                 LogFn log) {
    auto src_files = scan_lmu_files(game2_path);
    auto naming = detect_lmu_naming(output_path);

    int count = 0;
    for (auto& [old_id, new_id] : off.map_remap) {
        auto it = src_files.find(old_id);
        if (it == src_files.end()) continue;
        auto map = lcf::LMU_Reader::Load(it->second.string(), encoding);
        if (!map) continue;
        remap_map_events(*map, off);
        remap_map_asset_names(*map, asset_renames);
        fs::path dst = output_path / format_lmu_name(naming, new_id);
        if (lcf::LMU_Reader::Save(dst.string(), *map, engine, encoding)) ++count;
    }
    log("  Copied and remapped " + std::to_string(count) + " map files.");
    return count;
}

// ============================================================================
// Asset copying
// ============================================================================

static fs::path find_available_name(const fs::path& dst_dir, const fs::path& filename) {
    fs::path dst = dst_dir / filename;
    if (!fs::exists(dst)) return dst;
    std::string stem = filename.stem().string();
    std::string ext = filename.extension().string();
    for (int n = 1; ; ++n) {
        fs::path candidate = dst_dir / (stem + "_" + std::to_string(n) + ext);
        if (!fs::exists(candidate)) return candidate;
    }
}

static std::unordered_map<std::string, std::string> copy_assets(
    const fs::path& game2_path, const fs::path& output_path, LogFn log) {
    static const char* folders[] = {
        "CharSet", "ChipSet", "FaceSet", "Monster", "Panorama", "Picture",
        "System", "Title", "Backdrop", "Battle", "Battle2", "BattleCharSet",
        "BattleWeapon", "Frame", "GameOver", "System2", "Music", "Sound"
    };
    std::unordered_map<std::string, std::string> renames;
    int copied = 0, renamed = 0;
    for (auto& folder : folders) {
        fs::path src_dir = game2_path / folder;
        if (!fs::is_directory(src_dir)) continue;
        fs::path dst_dir = output_path / folder;
        fs::create_directories(dst_dir);
        for (auto& entry : fs::directory_iterator(src_dir)) {
            if (!entry.is_regular_file()) continue;
            fs::path filename = entry.path().filename();
            fs::path dst = find_available_name(dst_dir, filename);
            std::error_code ec;
            fs::copy_file(entry.path(), dst, ec);
            if (!ec) {
                ++copied;
                if (dst.filename() != filename) {
                    ++renamed;
                    renames[filename.stem().string()] = dst.stem().string();
                }
            }
        }
    }
    log("  Copied " + std::to_string(copied) + " asset files" +
        (renamed > 0 ? " (" + std::to_string(renamed) + " renamed)" : "") + ".");
    return renames;
}

// ============================================================================
// Accumulated merge state
// ============================================================================

struct MergeState {
    fs::path output_path;
    lcf::EngineVersion engine = lcf::EngineVersion::e2k;
    std::unique_ptr<lcf::rpg::Database> db;
    std::unique_ptr<lcf::rpg::TreeMap> tree;
    int maps_merged = 0;
    int db_entries_merged = 0;
    std::string encoding;
};

// ============================================================================
// Merge one game into accumulated state
// ============================================================================

static bool merge_single_game(MergeState& state, const fs::path& game_path,
                               bool merge_assets, bool is_first, LogFn log) {
    std::error_code ec;

    if (is_first) {
        state.output_path = game_path.parent_path() /
                            (game_path.filename().string() + "_merged");
        bool reuse_source = false;
        if (fs::exists(state.output_path)) {
            try { reuse_source = fs::equivalent(state.output_path, game_path); }
            catch (...) {}
        }
        if (reuse_source) {
            log("Using source folder as output (names match).");
            state.output_path = game_path;
        } else {
            if (fs::exists(state.output_path)) {
                log("Removing previous merged output...");
                fs::remove_all(state.output_path);
            }
            fs::create_directories(state.output_path);
            log("Copying base game to output folder...");
            fs::copy(game_path, state.output_path,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) { log("ERROR: Failed to copy: " + ec.message()); return false; }
        }
        log("  Output: " + state.output_path.string());
        state.engine = fs::is_directory(game_path / "Battle2")
                           ? lcf::EngineVersion::e2k3 : lcf::EngineVersion::e2k;
        state.db = lcf::LDB_Reader::Load(
            (state.output_path / "RPG_RT.ldb").string(), state.encoding);
        state.tree = lcf::LMT_Reader::Load(
            (state.output_path / "RPG_RT.lmt").string(), state.encoding);
        if (!state.db || !state.tree) {
            log("ERROR: Failed to load database or map tree.");
            return false;
        }
        return true;
    }

    if (!fs::exists(game_path / "RPG_RT.ldb") || !fs::exists(game_path / "RPG_RT.lmt")) {
        log("WARNING: Skipping " + game_path.filename().string() +
            " — missing RPG_RT.ldb or RPG_RT.lmt.");
        return false;
    }

    if (fs::is_directory(game_path / "Battle2") != (state.engine == lcf::EngineVersion::e2k3))
        log("WARNING: Engine mismatch for " + game_path.filename().string());

    auto db2 = lcf::LDB_Reader::Load((game_path / "RPG_RT.ldb").string(), state.encoding);
    auto tree2 = lcf::LMT_Reader::Load((game_path / "RPG_RT.lmt").string(), state.encoding);
    if (!db2 || !tree2) {
        log("WARNING: Failed to load " + game_path.filename().string() + " — skipping.");
        return false;
    }

    auto map_remap = build_map_remap(*state.tree, game_path, log);
    int maps_added = (int)map_remap.size();
    if (map_remap.empty()) {
        log("WARNING: No maps to remap for " + game_path.filename().string());
        return false;
    }

    IdOffsets off = calculate_offsets(*state.db, std::move(map_remap));
    int db_count = merge_database(*state.db, *db2, off, log);

    // Copy assets first so we know which names were renamed
    std::unordered_map<std::string, std::string> asset_renames;
    if (merge_assets)
        asset_renames = copy_assets(game_path, state.output_path, log);

    copy_renumbered_maps(game_path, state.output_path, off,
                          state.engine, state.encoding, asset_renames, log);

    merge_map_tree(*state.tree, *tree2, game_path, off, log);
    extend_existing_ranks_and_bitarrays(*state.db, off);
    remap_db_references(*state.db, off, log);
    remap_db_asset_names(*state.db, off, asset_renames);

    state.maps_merged += maps_added;
    state.db_entries_merged += db_count;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool validate_merge_folders(const std::vector<fs::path>& games, LogFn log) {
    if (games.size() < 2) { log("Need at least 2 games to merge."); return false; }
    for (size_t i = 0; i < games.size(); ++i) {
        auto name = games[i].filename().string();
        if (!fs::exists(games[i] / "RPG_RT.ldb")) { log(name + ": No RPG_RT.ldb found."); return false; }
        if (!fs::exists(games[i] / "RPG_RT.lmt")) { log(name + ": No RPG_RT.lmt found."); return false; }
    }
    return true;
}

MergeResult merge_games(const std::vector<fs::path>& games, bool merge_assets, LogFn log) {
    MergeResult result;
    if (!validate_merge_folders(games, log)) { result.message = "Validation failed."; return result; }

    MergeState state;
    state.encoding = resolve_encoding(games, log);

    for (size_t i = 0; i < games.size(); ++i) {
        bool is_first = (i == 0);
        auto name = games[i].filename().string();
        if (is_first) log("=== Base Game: " + name + " ===");
        else log("\n=== Merging Game " + std::to_string(i + 1) + ": " + name + " ===");

        if (!merge_single_game(state, games[i], merge_assets, is_first, log)) {
            if (is_first) { result.message = "Failed to initialize from base game."; return result; }
            continue;
        }
        result.games_merged = (int)i + 1;
    }

    if (state.db && state.tree) {
        log("\nSaving merged files...");
        lcf::LDB_Reader::Save((state.output_path / "RPG_RT.ldb").string(), *state.db, state.encoding);
        lcf::LMT_Reader::Save((state.output_path / "RPG_RT.lmt").string(), *state.tree, state.engine, state.encoding);
    }

    result.maps_merged = state.maps_merged;
    result.db_entries_merged = state.db_entries_merged;
    log("\n=== Merge Complete ===");
    log("  Games merged: " + std::to_string(result.games_merged));
    log("  Total maps: " + std::to_string(result.maps_merged));
    log("  Total DB entries: " + std::to_string(result.db_entries_merged));
    log("  Output: " + state.output_path.string());
    result.message = "Done — " + std::to_string(result.games_merged) + " games, " +
                     std::to_string(result.maps_merged) + " maps merged";
    return result;
}

} // namespace hate2k
