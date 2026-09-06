/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 */
#ifndef CORE_SKILLMATCHMEMORYREADER_HPP
#define CORE_SKILLMATCHMEMORYREADER_HPP

#include <cstdint>

class SkillMatchMemoryReader
{
  public:
    bool ReadRdramU8(uint32_t logicalAddress, uint8_t& value) const;
};

#endif // CORE_SKILLMATCHMEMORYREADER_HPP
