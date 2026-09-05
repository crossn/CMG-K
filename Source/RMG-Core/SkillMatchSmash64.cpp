/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 */
#include "SkillMatchSmash64.hpp"

#include "RomHeader.hpp"
#include "SkillMatchMemoryReader.hpp"

namespace
{
constexpr uint32_t Smash64JapanV10Crc1 = 0x67D20729;
constexpr uint32_t Smash64JapanV10Crc2 = 0xF696774C;
constexpr uint32_t Smash64JapanCountryCode = 0x4a;
constexpr uint32_t GameStatusAddress = 0x000A2CD9;
constexpr uint32_t P1StockCountAddress = 0x000A2CF3;
constexpr uint32_t P2StockCountAddress = 0x000A2D67;
constexpr uint32_t PauseInitiatorAddress = 0x0012F374;

std::string canonicalRomName(const std::string& name)
{
    std::string canonical = name;
    while (!canonical.empty() && (canonical.back() == ' ' || canonical.back() == '\0'))
    {
        canonical.pop_back();
    }
    return canonical;
}
}

bool SkillMatchIsSmash64JapanV10(const CoreRomHeader& header)
{
    return header.CRC1 == Smash64JapanV10Crc1 &&
           header.CRC2 == Smash64JapanV10Crc2 &&
           header.CountryCode == Smash64JapanCountryCode &&
           canonicalRomName(header.Name) == "SMASH BROTHERS";
}

bool SkillMatchReadSmash64JapanV10Snapshot(const SkillMatchMemoryReader& reader, unsigned int frame,
                                            SkillMatchSnapshot& snapshot)
{
    SkillMatchSnapshot readSnapshot;
    readSnapshot.Frame = frame;
    if (!reader.ReadRdramU8(GameStatusAddress, readSnapshot.GameStatus) ||
        !reader.ReadRdramU8(P1StockCountAddress, readSnapshot.P1StockRaw) ||
        !reader.ReadRdramU8(P2StockCountAddress, readSnapshot.P2StockRaw) ||
        !reader.ReadRdramU8(PauseInitiatorAddress, readSnapshot.PausePlayer))
    {
        return false;
    }

    snapshot = readSnapshot;
    return true;
}
