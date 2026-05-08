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
#include "RollbackNetcode.hpp"
#include "Emulation.hpp"
#include "Error.hpp"
#include "Library.hpp"

#include "m64p/Api.hpp"

CORE_EXPORT bool CoreRollbackSaveGameState(CoreRollbackState& state, int frame)
{
    std::string error;
    m64p_error ret;
    m64p_rollback_state coreState = {};

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    coreState.frame = frame;
    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_SAVE_STATE, 0, &coreState);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackSaveGameState DoCommand(M64CMD_ROLLBACK_SAVE_STATE) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    state.buffer = coreState.buffer;
    state.len = coreState.len;
    state.checksum = coreState.checksum;
    state.frame = frame;
    return true;
}

CORE_EXPORT bool CoreRollbackLoadGameState(const CoreRollbackState& state)
{
    std::string error;
    m64p_error ret;
    m64p_rollback_state coreState = {};

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    coreState.buffer = state.buffer;
    coreState.len = state.len;
    coreState.checksum = state.checksum;
    coreState.frame = state.frame;
    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_LOAD_STATE, 0, &coreState);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackLoadGameState DoCommand(M64CMD_ROLLBACK_LOAD_STATE) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT void CoreRollbackFreeGameState(CoreRollbackState& state)
{
    if (state.buffer != nullptr && m64p::Core.IsHooked())
    {
        m64p::Core.DoCommand(M64CMD_ROLLBACK_FREE_STATE, 0, state.buffer);
    }

    state = {};
}

CORE_EXPORT bool CoreRollbackAdvanceFrame(void)
{
    return CoreRunFrames(1, CoreFrameOutput_None);
}
