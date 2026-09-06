/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 */
#ifndef CORE_SKILLMATCHSMASH64_HPP
#define CORE_SKILLMATCHSMASH64_HPP

#include <cstdint>

class SkillMatchMemoryReader;
struct CoreRomHeader;

struct SkillMatchSnapshot
{
    unsigned int Frame = 0;
    uint8_t GameStatus = 0;
    uint8_t P1StockRaw = 0;
    uint8_t P2StockRaw = 0;
    uint8_t PausePlayer = 0;
};

bool SkillMatchIsSmash64JapanV10(const CoreRomHeader& header);
bool SkillMatchReadSmash64JapanV10Snapshot(const SkillMatchMemoryReader& reader, unsigned int frame,
                                            SkillMatchSnapshot& snapshot);

#endif // CORE_SKILLMATCHSMASH64_HPP
