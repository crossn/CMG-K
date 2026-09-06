/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 */
#include "SkillMatchMemoryReader.hpp"

#include "m64p/Api.hpp"
#include "m64p/api/m64p_debugger.h"

namespace
{
constexpr uint32_t RdramMaxSize = 0x800000;
}

bool SkillMatchMemoryReader::ReadRdramU8(uint32_t logicalAddress, uint8_t& value) const
{
    if (logicalAddress >= RdramMaxSize || m64p::Core.MemGetPointer == nullptr)
    {
        return false;
    }

    const auto* rdram = static_cast<const uint32_t*>(m64p::Core.MemGetPointer(M64P_DBG_PTR_RDRAM));
    if (rdram == nullptr)
    {
        return false;
    }

    const uint32_t word = rdram[logicalAddress >> 2];
    const uint32_t shift = (3 - (logicalAddress & 3)) * 8;
    value = static_cast<uint8_t>((word >> shift) & 0xff);
    return true;
}
