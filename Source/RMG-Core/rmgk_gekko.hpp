/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef RMGK_GEKKO_HPP
#define RMGK_GEKKO_HPP

#include "Emulation.hpp"
#include "RollbackNetcode.hpp"

#include <cstdint>
#include <string>

// Describes one remote peer in a lobby-driven session. Each non-local
// slot needs its own endpoint — GekkoNet's gekko_add_actor takes a
// per-actor address, and in a 3-/4-player room every remote talks to
// a different peer.
struct LobbyRemotePeer
{
    int slot = 0;              // 1-indexed N64 controller slot
    std::string ip;            // IPv4 dotted quad
    unsigned short port = 0;
};

class rmgk_gekko
{
  public:
    using InputProvider = bool (*)(uint32_t* inputs, int players, void* userData);
    using FrameCallback = bool (*)(void* userData);

    static bool start_p2p_session(const char* gameName, int players, int inputSize,
        int localPlayer, unsigned short localPort, const char* remoteIp, unsigned short remotePort, int localDelay, int predictionWindow);
    // Lobby variant: uses GekkoNet's built-in UDP adapter directly instead of
    // riding on n02's P2P socket. Takes an explicit list of remote peers
    // (slot + endpoint) so 3-/4-player sessions can wire each remote actor
    // to its own peer address.
    static bool start_lobby_session(const char* gameName, int players, int inputSize,
        int localPlayer, unsigned short localPort,
        const LobbyRemotePeer* remotes, int numRemotes,
        int localDelay, int predictionWindow);
    static bool start_local_session(const char* gameName, int players, int inputSize, int localDelay);
    static void close_session();
    static void request_stop();
    // Thread-safe: queue a remote slot (1-indexed N64 controller) to be force-
    // disconnected from the rollback session. The request is drained on the
    // emulation thread at the start of the next frame, so GekkoNet drops the
    // actor immediately instead of waiting out its 5 s silence timeout. Safe to
    // call from any thread (e.g. the lobby UI when the server reports a drop);
    // a no-op if there's no active session or the slot isn't a remote.
    static void request_disconnect_player(int slot);
    static bool is_netplay_session_active();
    static bool execute();
    static bool set_deterministic(bool enabled);
    static bool install_core_input_callback();
    static void clear_core_input_callback();
    static bool synchronize_input(void* values, int size, int players);
    static void set_debug_hooks(InputProvider inputProvider, FrameCallback beginFrame, FrameCallback endFrame, void* userData);
    static void set_debug_frame_output(int flags);
    static bool debug_run_frame_with_inputs(const uint32_t* inputs, int players, int flags);
    static bool toggle_client_input_replay();
};

#endif // RMGK_GEKKO_HPP
