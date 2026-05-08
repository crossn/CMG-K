/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef RMGK_GGPO_HPP
#define RMGK_GGPO_HPP

#include "Emulation.hpp"
#include "RollbackNetcode.hpp"

class rmgk_ggpo
{
  public:
    using SynchronizeInputCallback = bool (*)(void* values, int size, int players, void* userData);
    using AdvanceFrameCallback = bool (*)(int flags, void* userData);

    struct SessionCallbacks
    {
        SynchronizeInputCallback synchronize_input = nullptr;
        AdvanceFrameCallback advance_frame = nullptr;
    };

    static bool start_session(const SessionCallbacks& callbacks, void* userData);
    static bool start_synctest(const SessionCallbacks& callbacks, void* userData, const char* gameName, int players, int inputSize, int checkDistance);
    static bool start_p2p_session(const SessionCallbacks& callbacks, void* userData, const char* gameName, int players, int inputSize,
        int localPlayer, unsigned short localPort, const char* remoteIp, unsigned short remotePort, int frameDelay);
    static void close_session();
    static bool idle(int timeoutMs);
    static bool is_session_running();
    static bool is_real_session_running();
    static bool is_ready_to_advance();

    static bool save_game_state(CoreRollbackState& state, int frame);
    static bool load_game_state(const CoreRollbackState& state);
    static void free_buffer(CoreRollbackState& state);
    static bool advance_frame(int frameOutputFlags = CoreFrameOutput_None);
    static bool advance_frames(int frames, int frameOutputFlags = CoreFrameOutput_None);
    static bool execute();
    static bool set_deterministic(bool enabled);

    static void set_synchronize_input_callback(SynchronizeInputCallback callback, void* userData);
    static bool install_core_input_callback();
    static void clear_core_input_callback();
    static bool synchronize_input(void* values, int size, int players);
};

#endif // RMGK_GGPO_HPP
