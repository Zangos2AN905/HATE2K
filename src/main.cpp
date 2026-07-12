#include "app.h"
#include "corruptor.h"
#include "dialogue.h"

#include <slint.h>

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace fs = std::filesystem;

static std::string pick_folder_native() {
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return "";

    IFileDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) { CoUninitialize(); return ""; }

    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(L"Select RPG Maker Game Folder");

    std::string result;
    if (SUCCEEDED(pfd->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) {
            PWSTR wpath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    result.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, result.data(), len, nullptr, nullptr);
                }
                CoTaskMemFree(wpath);
            }
            item->Release();
        }
    }
    pfd->Release();
    CoUninitialize();
    return result;
#else
    return "";
#endif
}

int main() {
    auto ui = MainWindow::create();

    std::string current_game_path;
    bool current_is_rm2003 = false;

    // ── Folder picker ──────────────────────────────────────────────────
    ui->on_select_folder([&]() {
        std::string path = pick_folder_native();
        if (path.empty()) return;

        current_game_path = path;
        ui->set_game_path(slint::SharedString(path));

        if (hate2k::detect_game(path)) {
            current_is_rm2003 = hate2k::is_rm2003(path);
            ui->set_game_detected(true);
            ui->set_engine_label(slint::SharedString(
                current_is_rm2003 ? "RPG Maker 2003 detected" : "RPG Maker 2000 detected"));
            ui->set_status(slint::SharedString("Game loaded — select options and press Shuffle."));
        } else {
            ui->set_game_detected(false);
            ui->set_engine_label(slint::SharedString(""));
            ui->set_status(slint::SharedString("No RPG_RT.ldb found in the selected folder."));
        }
        ui->set_log_text(slint::SharedString(""));
    });

    // ── Shuffle (assets first, then dialogue) ──────────────────────────
    ui->on_shuffle_requested([&]() {
        if (current_game_path.empty()) return;

        ui->set_is_processing(true);
        ui->set_status(slint::SharedString("Shuffling…"));

        std::vector<bool> asset_enabled = {
            ui->get_shuffle_charset(),
            ui->get_shuffle_chipset(),
            ui->get_shuffle_faceset(),
            ui->get_shuffle_monster(),
            ui->get_shuffle_panorama(),
            ui->get_shuffle_picture(),
            ui->get_shuffle_system(),
            ui->get_shuffle_title(),
            ui->get_shuffle_backdrop(),
            ui->get_shuffle_battle(),
            ui->get_shuffle_battle2(),
            ui->get_shuffle_battlecharset(),
            ui->get_shuffle_battleweapon(),
            ui->get_shuffle_frame(),
            ui->get_shuffle_gameover(),
            ui->get_shuffle_system2(),
            ui->get_shuffle_music(),
            ui->get_shuffle_sound(),
        };

        bool do_across  = ui->get_dlg_across_maps();
        bool do_within  = ui->get_dlg_within_maps();
        bool do_words   = ui->get_dlg_shuffle_words();
        bool do_case    = ui->get_dlg_random_case();
        bool do_markov  = ui->get_dlg_markov();
        bool do_transfers = ui->get_data_transfers();
        bool do_characters = ui->get_data_characters();
        bool backup     = ui->get_backup_enabled();
        fs::path game_path = current_game_path;

        std::thread([&, game_path, asset_enabled,
                     do_across, do_within, do_words, do_case, do_markov,
                     do_transfers, do_characters,
                     backup]() {
            std::ostringstream log_stream;
            auto log_fn = [&log_stream](const std::string& msg) {
                log_stream << msg << "\n";
            };

            try {
                // Backup once before everything
                if (backup) {
                    hate2k::backup_game(game_path, log_fn);
                }

                int asset_total = 0;
                int dlg_total = 0;

                // Phase 1: asset shuffle
                log_fn("── Asset Shuffling ──");
                auto results = hate2k::shuffle_all(game_path, asset_enabled, false, log_fn);
                for (auto& r : results) asset_total += r.files_shuffled;

                // Phase 2: dialogue shuffle
                log_fn("\n── Dialogue Shuffling ──");
                std::random_device rd;
                std::mt19937 rng(rd());

                if (do_across) {
                    log_fn("Engine: Shuffle across maps");
                    auto r = hate2k::shuffle_dialogue_across_maps(game_path, rng, log_fn);
                    log_fn("  " + r.message);
                    dlg_total += r.messages_shuffled;
                }
                if (do_within) {
                    log_fn("Engine: Shuffle within maps");
                    auto r = hate2k::shuffle_dialogue_within_maps(game_path, rng, log_fn);
                    log_fn("  " + r.message);
                    dlg_total += r.messages_shuffled;
                }
                if (do_words) {
                    log_fn("Engine: Shuffle words");
                    auto r = hate2k::shuffle_words(game_path, rng, log_fn);
                    log_fn("  " + r.message);
                    dlg_total += r.messages_shuffled;
                }
                if (do_case) {
                    log_fn("Engine: Random case");
                    auto r = hate2k::shuffle_case(game_path, rng, log_fn);
                    log_fn("  " + r.message);
                    dlg_total += r.messages_shuffled;
                }
                if (do_markov) {
                    log_fn("Engine: Markov nonsense");
                    auto r = hate2k::markov_nonsense(game_path, rng, log_fn);
                    log_fn("  " + r.message);
                    dlg_total += r.messages_shuffled;
                }

                // Phase 3: data shuffling
                if (do_transfers || do_characters) {
                    log_fn("\n── Data Shuffling ──");
                }
                if (do_transfers) {
                    log_fn("Shuffle: Map transfers");
                    auto r = hate2k::shuffle_transfers(game_path, rng, log_fn);
                    log_fn("  " + r.message);
                    dlg_total += r.messages_shuffled;
                }
                if (do_characters) {
                    log_fn("Shuffle: Player characters");
                    auto r = hate2k::shuffle_characters(game_path, rng, log_fn);
                    log_fn("  " + r.message);
                    dlg_total += r.messages_shuffled;
                }

                log_stream << "\n=== Summary ===\n";
                log_stream << asset_total << " asset files shuffled, "
                           << dlg_total << " dialogue messages modified.\n";

                std::string final_log = log_stream.str();
                std::string final_status = "Done — " + std::to_string(asset_total) +
                                           " assets, " + std::to_string(dlg_total) + " dialogue.";

                slint::invoke_from_event_loop([&, final_log, final_status]() {
                    ui->set_log_text(slint::SharedString(final_log));
                    ui->set_status(slint::SharedString(final_status));
                    ui->set_is_processing(false);
                });
            } catch (const std::exception& ex) {
                log_stream << "\nERROR: " << ex.what() << "\n";
                std::string final_log = log_stream.str();
                slint::invoke_from_event_loop([&, final_log]() {
                    ui->set_log_text(slint::SharedString(final_log));
                    ui->set_status(slint::SharedString("Error — see log for details."));
                    ui->set_is_processing(false);
                });
            } catch (...) {
                std::string final_log = log_stream.str() + "\nERROR: Unknown crash caught.\n";
                slint::invoke_from_event_loop([&, final_log]() {
                    ui->set_log_text(slint::SharedString(final_log));
                    ui->set_status(slint::SharedString("Unknown error."));
                    ui->set_is_processing(false);
                });
            }
        }).detach();
    });

    // ── Restore backup ─────────────────────────────────────────────────
    ui->on_restore_backup_requested([&]() {
        if (current_game_path.empty()) return;

        ui->set_is_processing(true);
        ui->set_status(slint::SharedString("Restoring backup…"));

        fs::path game_path = current_game_path;

        std::thread([&, game_path]() {
            std::ostringstream log_stream;
            auto log_fn = [&log_stream](const std::string& msg) {
                log_stream << msg << "\n";
            };

            try {
                bool ok = hate2k::restore_backup(game_path, log_fn);

                std::string final_log = log_stream.str();
                std::string final_status = ok ? "Backup restored." : "Restore failed — no backup found.";

                slint::invoke_from_event_loop([&, final_log, final_status]() {
                    ui->set_log_text(slint::SharedString(final_log));
                    ui->set_status(slint::SharedString(final_status));
                    ui->set_is_processing(false);
                });
            } catch (const std::exception& ex) {
                std::string final_log = log_stream.str() + "\nERROR: " + ex.what() + "\n";
                slint::invoke_from_event_loop([&, final_log]() {
                    ui->set_log_text(slint::SharedString(final_log));
                    ui->set_status(slint::SharedString("Restore error."));
                    ui->set_is_processing(false);
                });
            }
        }).detach();
    });

    ui->run();
    return 0;
}
