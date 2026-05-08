/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#define CORE_INTERNAL
#include "rmgk_ggpo.hpp"

#include "Library.hpp"

#ifdef RMGK_HAVE_GGPO
#include <ggponet.h>
#endif

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
rmgk_ggpo::SessionCallbacks g_SessionCallbacks;
void* g_SessionUserData = nullptr;
bool g_SessionRunning = false;
#ifdef RMGK_HAVE_GGPO
GGPOSession* g_GgpoSession = nullptr;
int g_GgpoPlayers = 0;
int g_GgpoInputSize = 0;
bool g_GgpoInAdvanceCallback = false;
bool g_GgpoAggregateLocalInput = false;
int g_GgpoLocalPlayer = 0;
GGPOPlayerHandle g_GgpoLocalPlayerHandle = GGPO_INVALID_HANDLE;
std::vector<uint32_t> g_GgpoLatchedInput;
bool g_GgpoHasLatchedInput = false;
bool g_GgpoReadyToAdvance = false;
bool g_GgpoCoreExecuteActive = false;
std::mutex g_GgpoInputLogMutex;
uint32_t g_GgpoLastSubmittedInput = 0xffffffffu;
std::vector<uint32_t> g_GgpoLastLatchedInput;
int g_GgpoInputLogFrames = 0;
#endif

int rmgk_ggpo_core_input_callback(void* values, int size, int players)
{
    return rmgk_ggpo::synchronize_input(values, size, players) ? 1 : 0;
}

#ifdef RMGK_HAVE_GGPO
std::string hex_input(uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

void reset_ggpo_input_log()
{
    std::lock_guard<std::mutex> lock(g_GgpoInputLogMutex);
    std::ofstream file("rollback_ggpo_input.log", std::ios::out | std::ios::trunc);
    file << "RMG-K GGPO input log\n";
    g_GgpoLastSubmittedInput = 0xffffffffu;
    g_GgpoLastLatchedInput.clear();
    g_GgpoInputLogFrames = 0;
}

void write_ggpo_input_log(const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_GgpoInputLogMutex);
    std::ofstream file("rollback_ggpo_input.log", std::ios::out | std::ios::app);
    file << "core_frame=" << CoreGetCurrentFrameCount() << " " << message << "\n";
}

const char* ggpo_event_name(GGPOEventCode code)
{
    switch (code)
    {
    case GGPO_EVENTCODE_CONNECTED_TO_PEER: return "connected_to_peer";
    case GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER: return "synchronizing_with_peer";
    case GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER: return "synchronized_with_peer";
    case GGPO_EVENTCODE_RUNNING: return "running";
    case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER: return "disconnected_from_peer";
    case GGPO_EVENTCODE_TIMESYNC: return "timesync";
    case GGPO_EVENTCODE_CONNECTION_INTERRUPTED: return "connection_interrupted";
    case GGPO_EVENTCODE_CONNECTION_RESUMED: return "connection_resumed";
    default: return "unknown";
    }
}

bool run_core_frame_blocking(int frameOutputFlags)
{
    const int frameBefore = CoreGetCurrentFrameCount();
    if (!CoreRunFrames(1, frameOutputFlags))
    {
        return false;
    }

    constexpr int maxWaitMs = 5000;
    for (int elapsedMs = 0; elapsedMs < maxWaitMs; elapsedMs++)
    {
        if (CoreGetCurrentFrameCount() != frameBefore && CoreIsEmulationPaused())
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

int ggpo_input_bytes()
{
    return g_GgpoInputSize * g_GgpoPlayers;
}

int ggpo_input_words()
{
    return (ggpo_input_bytes() + static_cast<int>(sizeof(uint32_t)) - 1) / static_cast<int>(sizeof(uint32_t));
}

bool latch_ggpo_input_for_current_frame()
{
    if (g_GgpoSession == nullptr)
    {
        return false;
    }

    g_GgpoLatchedInput.assign(ggpo_input_words(), 0);
    int disconnectFlags = 0;
    if (!GGPO_SUCCEEDED(ggpo_synchronize_input(g_GgpoSession, g_GgpoLatchedInput.data(), ggpo_input_bytes(), &disconnectFlags)))
    {
        write_ggpo_input_log("sync_input result=fail");
        g_GgpoHasLatchedInput = false;
        return false;
    }

    g_GgpoHasLatchedInput = true;
    if (g_GgpoInputLogFrames < 600 || g_GgpoLatchedInput != g_GgpoLastLatchedInput)
    {
        std::ostringstream stream;
        stream << "sync_input result=ok disconnect_flags=" << disconnectFlags;
        for (int player = 0; player < g_GgpoPlayers; player++)
        {
            stream << " p" << (player + 1) << "=" << hex_input(g_GgpoLatchedInput[static_cast<size_t>(player)]);
        }
        write_ggpo_input_log(stream.str());
        g_GgpoLastLatchedInput = g_GgpoLatchedInput;
        g_GgpoInputLogFrames++;
    }
    return true;
}

bool add_local_input_for_next_frame()
{
    if (g_GgpoSession == nullptr)
    {
        return false;
    }

    if (!g_GgpoAggregateLocalInput && g_SessionCallbacks.synchronize_input == nullptr)
    {
        uint32_t input = 0;
        if (!CoreRollbackSampleInput(&input, g_GgpoInputSize, 1))
        {
            write_ggpo_input_log("add_local_input sample=fail");
            return false;
        }

        const bool added = GGPO_SUCCEEDED(ggpo_add_local_input(g_GgpoSession, g_GgpoLocalPlayerHandle, &input, g_GgpoInputSize));
        if (g_GgpoInputLogFrames < 600 || input != g_GgpoLastSubmittedInput || !added)
        {
            std::ostringstream stream;
            stream << "add_local_input result=" << (added ? "ok" : "fail")
                   << " local_player=" << g_GgpoLocalPlayer
                   << " handle=" << g_GgpoLocalPlayerHandle
                   << " physical_p1=" << hex_input(input);
            write_ggpo_input_log(stream.str());
            g_GgpoLastSubmittedInput = input;
        }
        return added;
    }

    std::vector<uint32_t> input(ggpo_input_words());
    if (g_SessionCallbacks.synchronize_input != nullptr)
    {
        if (!g_SessionCallbacks.synchronize_input(input.data(), g_GgpoInputSize, g_GgpoPlayers, g_SessionUserData))
        {
            return false;
        }
    }
    else if (!CoreRollbackSampleInput(input.data(), g_GgpoInputSize, g_GgpoPlayers))
    {
        return false;
    }

    if (g_GgpoAggregateLocalInput)
    {
        return GGPO_SUCCEEDED(ggpo_add_local_input(g_GgpoSession, g_GgpoLocalPlayerHandle, input.data(), ggpo_input_bytes()));
    }

    if (g_GgpoLocalPlayer < 1 || g_GgpoLocalPlayer > g_GgpoPlayers)
    {
        return false;
    }

    auto* inputBytes = reinterpret_cast<unsigned char*>(input.data());
    return GGPO_SUCCEEDED(ggpo_add_local_input(
        g_GgpoSession,
        g_GgpoLocalPlayerHandle,
        inputBytes + ((g_GgpoLocalPlayer - 1) * g_GgpoInputSize),
        g_GgpoInputSize));
}

bool prepare_forward_frame_input()
{
    return add_local_input_for_next_frame() && latch_ggpo_input_for_current_frame();
}

int rollback_execute_begin_frame(void* userData)
{
    (void)userData;
    if (g_GgpoSession == nullptr)
    {
        return 0;
    }

    while (g_GgpoSession != nullptr && !rmgk_ggpo::is_ready_to_advance())
    {
        rmgk_ggpo::idle(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (g_GgpoSession == nullptr)
    {
        return 0;
    }

    return prepare_forward_frame_input() ? 1 : 0;
}

int rollback_execute_end_frame(void* userData)
{
    (void)userData;
    if (g_GgpoSession == nullptr)
    {
        return 0;
    }

    const bool frameAdvanced = GGPO_SUCCEEDED(ggpo_advance_frame(g_GgpoSession));
    g_GgpoHasLatchedInput = false;
    return frameAdvanced ? 1 : 0;
}

bool prepare_rollback_frame_input()
{
    return latch_ggpo_input_for_current_frame();
}

bool __cdecl rmgk_ggpo_begin_game_callback(const char* game)
{
    (void)game;
    return true;
}

bool __cdecl rmgk_ggpo_save_game_state_callback(unsigned char** buffer, int* len, int* checksum, int frame)
{
    CoreRollbackState state;

    if (!CoreRollbackSaveGameState(state, frame))
    {
        return false;
    }

    *buffer = state.buffer;
    *len = state.len;
    *checksum = state.checksum;
    return true;
}

bool __cdecl rmgk_ggpo_load_game_state_callback(unsigned char* buffer, int len)
{
    CoreRollbackState state;
    state.buffer = buffer;
    state.len = len;
    return CoreRollbackLoadGameState(state);
}

bool __cdecl rmgk_ggpo_log_game_state_callback(char* filename, unsigned char* buffer, int len)
{
    (void)filename;
    (void)buffer;
    (void)len;
    return true;
}

void __cdecl rmgk_ggpo_free_buffer_callback(void* buffer)
{
    CoreRollbackState state;
    state.buffer = static_cast<unsigned char*>(buffer);
    CoreRollbackFreeGameState(state);
}

bool __cdecl rmgk_ggpo_advance_frame_callback(int flags)
{
    if (g_GgpoSession == nullptr)
    {
        return false;
    }

    if (g_GgpoCoreExecuteActive)
    {
        write_ggpo_input_log("rollback_callback result=fail reason=core_execute_sync_step_missing");
        return false;
    }

    g_GgpoInAdvanceCallback = true;
    const bool advanced = prepare_rollback_frame_input() && run_core_frame_blocking(flags);
    g_GgpoInAdvanceCallback = false;
    if (!advanced)
    {
        return false;
    }

    const bool frameAdvanced = GGPO_SUCCEEDED(ggpo_advance_frame(g_GgpoSession));
    g_GgpoHasLatchedInput = false;
    return frameAdvanced;
}

bool __cdecl rmgk_ggpo_on_event_callback(GGPOEvent* info)
{
    if (info != nullptr)
    {
        std::ostringstream stream;
        stream << "event code=" << static_cast<int>(info->code) << " name=" << ggpo_event_name(info->code);
        switch (info->code)
        {
        case GGPO_EVENTCODE_CONNECTED_TO_PEER:
            stream << " player_handle=" << info->u.connected.player;
            break;
        case GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER:
            stream << " player_handle=" << info->u.synchronizing.player
                   << " count=" << info->u.synchronizing.count
                   << " total=" << info->u.synchronizing.total;
            break;
        case GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER:
            stream << " player_handle=" << info->u.synchronized.player;
            break;
        case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
            stream << " player_handle=" << info->u.disconnected.player;
            g_GgpoReadyToAdvance = false;
            break;
        case GGPO_EVENTCODE_TIMESYNC:
            stream << " frames_ahead=" << info->u.timesync.frames_ahead;
            break;
        case GGPO_EVENTCODE_CONNECTION_INTERRUPTED:
            stream << " player_handle=" << info->u.connection_interrupted.player
                   << " timeout=" << info->u.connection_interrupted.disconnect_timeout;
            break;
        case GGPO_EVENTCODE_CONNECTION_RESUMED:
            stream << " player_handle=" << info->u.connection_resumed.player;
            break;
        default:
            break;
        }
        if (info->code == GGPO_EVENTCODE_RUNNING)
        {
            g_GgpoReadyToAdvance = true;
        }
        write_ggpo_input_log(stream.str());
    }
    return true;
}

GGPOSessionCallbacks make_ggpo_callbacks()
{
    GGPOSessionCallbacks callbacks = {};
    callbacks.begin_game = rmgk_ggpo_begin_game_callback;
    callbacks.save_game_state = rmgk_ggpo_save_game_state_callback;
    callbacks.load_game_state = rmgk_ggpo_load_game_state_callback;
    callbacks.log_game_state = rmgk_ggpo_log_game_state_callback;
    callbacks.free_buffer = rmgk_ggpo_free_buffer_callback;
    callbacks.advance_frame = rmgk_ggpo_advance_frame_callback;
    callbacks.on_event = rmgk_ggpo_on_event_callback;
    return callbacks;
}
#endif
} // namespace

CORE_EXPORT bool rmgk_ggpo::start_session(const SessionCallbacks& callbacks, void* userData)
{
    g_SessionCallbacks = callbacks;
    g_SessionUserData = userData;
    g_SessionRunning = true;

    if (!install_core_input_callback())
    {
        g_SessionCallbacks = {};
        g_SessionUserData = nullptr;
        g_SessionRunning = false;
        return false;
    }

    return true;
}

CORE_EXPORT bool rmgk_ggpo::start_synctest(const SessionCallbacks& callbacks, void* userData, const char* gameName, int players, int inputSize, int checkDistance)
{
#ifndef RMGK_HAVE_GGPO
    (void)callbacks;
    (void)userData;
    (void)gameName;
    (void)players;
    (void)inputSize;
    (void)checkDistance;
    return false;
#else
    close_session();

    if (gameName == nullptr || players < 1 || inputSize < 1 || checkDistance < 1)
    {
        return false;
    }

    g_SessionCallbacks = callbacks;
    g_SessionUserData = userData;
    g_GgpoPlayers = players;
    g_GgpoInputSize = inputSize;
    g_GgpoAggregateLocalInput = true;
    g_GgpoLocalPlayer = 1;
    g_GgpoReadyToAdvance = false;

    GGPOSessionCallbacks ggpoCallbacks = make_ggpo_callbacks();
    std::string mutableGameName = gameName;
    const int ggpoInputSize = players * inputSize;
    if (!GGPO_SUCCEEDED(ggpo_start_synctest(&g_GgpoSession, &ggpoCallbacks, mutableGameName.data(), 1, ggpoInputSize, checkDistance)))
    {
        g_SessionCallbacks = {};
        g_SessionUserData = nullptr;
        g_GgpoPlayers = 0;
        g_GgpoInputSize = 0;
        g_GgpoAggregateLocalInput = false;
        g_GgpoLocalPlayer = 0;
        g_GgpoReadyToAdvance = false;
        return false;
    }
    g_GgpoLocalPlayerHandle = 0;

    g_SessionRunning = true;
    if (!install_core_input_callback())
    {
        close_session();
        return false;
    }

    return idle(0);
#endif
}

CORE_EXPORT bool rmgk_ggpo::start_p2p_session(const SessionCallbacks& callbacks, void* userData, const char* gameName, int players, int inputSize,
    int localPlayer, unsigned short localPort, const char* remoteIp, unsigned short remotePort, int frameDelay)
{
#ifndef RMGK_HAVE_GGPO
    (void)callbacks;
    (void)userData;
    (void)gameName;
    (void)players;
    (void)inputSize;
    (void)localPlayer;
    (void)localPort;
    (void)remoteIp;
    (void)remotePort;
    (void)frameDelay;
    return false;
#else
    close_session();
    reset_ggpo_input_log();

    if (gameName == nullptr || players < 2 || inputSize < 1 || localPlayer < 1 || localPlayer > players ||
        remoteIp == nullptr || remoteIp[0] == '\0' || remotePort == 0)
    {
        write_ggpo_input_log("start_p2p_session invalid_params");
        return false;
    }

    {
        std::ostringstream stream;
        stream << "start_p2p_session game=" << gameName
               << " players=" << players
               << " input_size=" << inputSize
               << " local_player=" << localPlayer
               << " local_port=" << localPort
               << " remote_ip=" << remoteIp
               << " remote_port=" << remotePort
               << " frame_delay=" << frameDelay;
        write_ggpo_input_log(stream.str());
    }

    g_SessionCallbacks = callbacks;
    g_SessionUserData = userData;
    g_GgpoPlayers = players;
    g_GgpoInputSize = inputSize;
    g_GgpoAggregateLocalInput = false;
    g_GgpoLocalPlayer = localPlayer;
    g_GgpoReadyToAdvance = false;

    GGPOSessionCallbacks ggpoCallbacks = make_ggpo_callbacks();
    std::string mutableGameName = gameName;
    if (!GGPO_SUCCEEDED(ggpo_start_session(&g_GgpoSession, &ggpoCallbacks, mutableGameName.data(), players, inputSize, localPort)))
    {
        write_ggpo_input_log("ggpo_start_session result=fail");
        close_session();
        return false;
    }
    write_ggpo_input_log("ggpo_start_session result=ok");

    for (int player = 1; player <= players; player++)
    {
        GGPOPlayer ggpoPlayer = {};
        ggpoPlayer.size = sizeof(GGPOPlayer);
        ggpoPlayer.player_num = player;
        if (player == localPlayer)
        {
            ggpoPlayer.type = GGPO_PLAYERTYPE_LOCAL;
        }
        else
        {
            ggpoPlayer.type = GGPO_PLAYERTYPE_REMOTE;
            std::strncpy(ggpoPlayer.u.remote.ip_address, remoteIp, sizeof(ggpoPlayer.u.remote.ip_address) - 1);
            ggpoPlayer.u.remote.ip_address[sizeof(ggpoPlayer.u.remote.ip_address) - 1] = '\0';
            ggpoPlayer.u.remote.port = remotePort;
        }

        GGPOPlayerHandle handle = GGPO_INVALID_HANDLE;
        if (!GGPO_SUCCEEDED(ggpo_add_player(g_GgpoSession, &ggpoPlayer, &handle)))
        {
            std::ostringstream stream;
            stream << "ggpo_add_player result=fail player=" << player;
            write_ggpo_input_log(stream.str());
            close_session();
            return false;
        }

        {
            std::ostringstream stream;
            stream << "ggpo_add_player result=ok player=" << player
                   << " type=" << (player == localPlayer ? "local" : "remote")
                   << " handle=" << handle;
            if (player != localPlayer)
            {
                stream << " remote_ip=" << remoteIp << " remote_port=" << remotePort;
            }
            write_ggpo_input_log(stream.str());
        }

        if (player == localPlayer)
        {
            g_GgpoLocalPlayerHandle = handle;
            if (!GGPO_SUCCEEDED(ggpo_set_frame_delay(g_GgpoSession, handle, frameDelay)))
            {
                write_ggpo_input_log("ggpo_set_frame_delay result=fail");
                close_session();
                return false;
            }
            write_ggpo_input_log("ggpo_set_frame_delay result=ok");
        }
    }

    if (g_GgpoLocalPlayerHandle == GGPO_INVALID_HANDLE)
    {
        write_ggpo_input_log("start_p2p_session no_local_handle");
        close_session();
        return false;
    }

    ggpo_set_disconnect_timeout(g_GgpoSession, 3000);
    ggpo_set_disconnect_notify_start(g_GgpoSession, 1000);

    g_SessionRunning = true;
    if (!install_core_input_callback())
    {
        write_ggpo_input_log("install_core_input_callback result=fail");
        close_session();
        return false;
    }

    write_ggpo_input_log("install_core_input_callback result=ok");
    return idle(0);
#endif
}

CORE_EXPORT void rmgk_ggpo::close_session()
{
#ifdef RMGK_HAVE_GGPO
    if (g_GgpoSession != nullptr)
    {
        ggpo_close_session(g_GgpoSession);
        g_GgpoSession = nullptr;
        g_GgpoPlayers = 0;
        g_GgpoInputSize = 0;
        g_GgpoInAdvanceCallback = false;
        g_GgpoAggregateLocalInput = false;
        g_GgpoLocalPlayer = 0;
        g_GgpoLocalPlayerHandle = GGPO_INVALID_HANDLE;
        g_GgpoLatchedInput.clear();
        g_GgpoHasLatchedInput = false;
        g_GgpoReadyToAdvance = false;
        g_GgpoCoreExecuteActive = false;
    }
#endif
    clear_core_input_callback();
    g_SessionCallbacks = {};
    g_SessionUserData = nullptr;
    g_SessionRunning = false;
}

CORE_EXPORT bool rmgk_ggpo::idle(int timeoutMs)
{
#ifdef RMGK_HAVE_GGPO
    if (g_GgpoSession != nullptr)
    {
        return GGPO_SUCCEEDED(ggpo_idle(g_GgpoSession, timeoutMs));
    }
#endif
    (void)timeoutMs;
    return g_SessionRunning;
}

CORE_EXPORT bool rmgk_ggpo::is_session_running()
{
    return g_SessionRunning;
}

CORE_EXPORT bool rmgk_ggpo::is_real_session_running()
{
#ifdef RMGK_HAVE_GGPO
    return g_GgpoSession != nullptr;
#else
    return false;
#endif
}

CORE_EXPORT bool rmgk_ggpo::is_ready_to_advance()
{
#ifdef RMGK_HAVE_GGPO
    return g_GgpoSession != nullptr && (g_GgpoAggregateLocalInput || g_GgpoReadyToAdvance);
#else
    return false;
#endif
}

CORE_EXPORT bool rmgk_ggpo::save_game_state(CoreRollbackState& state, int frame)
{
    return CoreRollbackSaveGameState(state, frame);
}

CORE_EXPORT bool rmgk_ggpo::load_game_state(const CoreRollbackState& state)
{
    return CoreRollbackLoadGameState(state);
}

CORE_EXPORT void rmgk_ggpo::free_buffer(CoreRollbackState& state)
{
    CoreRollbackFreeGameState(state);
}

CORE_EXPORT bool rmgk_ggpo::advance_frame(int frameOutputFlags)
{
#ifdef RMGK_HAVE_GGPO
    if (g_GgpoSession != nullptr)
    {
        if (g_GgpoInAdvanceCallback)
        {
            return run_core_frame_blocking(frameOutputFlags);
        }

        idle(0);
        if (!is_ready_to_advance())
        {
            return true;
        }

        if (!prepare_forward_frame_input())
        {
            return false;
        }

        if (!run_core_frame_blocking(frameOutputFlags))
        {
            return false;
        }

        const bool frameAdvanced = GGPO_SUCCEEDED(ggpo_advance_frame(g_GgpoSession));
        g_GgpoHasLatchedInput = false;
        return frameAdvanced;
    }
#endif
    if (g_SessionCallbacks.advance_frame != nullptr)
    {
        return g_SessionCallbacks.advance_frame(frameOutputFlags, g_SessionUserData);
    }

    return CoreRunFrames(1, frameOutputFlags);
}

CORE_EXPORT bool rmgk_ggpo::advance_frames(int frames, int frameOutputFlags)
{
    if (frames < 1)
    {
        frames = 1;
    }

#ifdef RMGK_HAVE_GGPO
    if (g_GgpoSession == nullptr && g_SessionCallbacks.advance_frame == nullptr)
#else
    if (g_SessionCallbacks.advance_frame == nullptr)
#endif
    {
        return CoreRunFrames(frames, frameOutputFlags);
    }

    for (int frame = 0; frame < frames; frame++)
    {
        if (!advance_frame(frameOutputFlags))
        {
            return false;
        }
    }

    return true;
}

CORE_EXPORT bool rmgk_ggpo::execute()
{
#ifdef RMGK_HAVE_GGPO
    if (g_GgpoSession == nullptr)
    {
        return false;
    }

    m64p_rollback_execute_callbacks callbacks = {};
    callbacks.begin_frame = rollback_execute_begin_frame;
    callbacks.end_frame = rollback_execute_end_frame;
    g_GgpoCoreExecuteActive = true;
    const bool executed = CoreRollbackExecute(callbacks);
    g_GgpoCoreExecuteActive = false;
    return executed;
#else
    return false;
#endif
}

CORE_EXPORT bool rmgk_ggpo::set_deterministic(bool enabled)
{
    return CoreRollbackSetDeterministic(enabled);
}

CORE_EXPORT void rmgk_ggpo::set_synchronize_input_callback(SynchronizeInputCallback callback, void* userData)
{
    g_SessionCallbacks.synchronize_input = callback;
    g_SessionUserData = userData;
}

CORE_EXPORT bool rmgk_ggpo::install_core_input_callback()
{
    return CoreRollbackSetInputCallback(rmgk_ggpo_core_input_callback);
}

CORE_EXPORT void rmgk_ggpo::clear_core_input_callback()
{
    CoreRollbackSetInputCallback(nullptr);
    g_SessionCallbacks = {};
    g_SessionUserData = nullptr;
    g_SessionRunning = false;
}

CORE_EXPORT bool rmgk_ggpo::synchronize_input(void* values, int size, int players)
{
#ifdef RMGK_HAVE_GGPO
    if (g_GgpoSession != nullptr)
    {
        if (values == nullptr || size != g_GgpoInputSize || players < g_GgpoPlayers)
        {
            write_ggpo_input_log("pif_sync result=fail reason=shape");
            return false;
        }

        if (!g_GgpoHasLatchedInput || g_GgpoLatchedInput.size() < static_cast<size_t>(ggpo_input_words()))
        {
            write_ggpo_input_log("pif_sync result=fail reason=no_latched_input");
            return false;
        }

        std::memset(values, 0, static_cast<size_t>(size) * static_cast<size_t>(players));
        std::memcpy(values, g_GgpoLatchedInput.data(), ggpo_input_bytes());
        if (g_GgpoInputLogFrames < 600)
        {
            std::ostringstream stream;
            stream << "pif_sync result=ok requested_players=" << players;
            for (int player = 0; player < g_GgpoPlayers; player++)
            {
                stream << " pif_slot" << (player + 1) << "=" << hex_input(g_GgpoLatchedInput[static_cast<size_t>(player)]);
            }
            write_ggpo_input_log(stream.str());
        }
        return true;
    }
#endif
    if (g_SessionCallbacks.synchronize_input == nullptr)
    {
        return true;
    }

    return g_SessionCallbacks.synchronize_input(values, size, players, g_SessionUserData);
}
