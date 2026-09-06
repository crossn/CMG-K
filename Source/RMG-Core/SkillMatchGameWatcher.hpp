/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 */
#ifndef CORE_SKILLMATCHGAMEWATCHER_HPP
#define CORE_SKILLMATCHGAMEWATCHER_HPP

#include "SkillMatchSmash64.hpp"

#include <vector>

enum class SkillMatchEventType
{
    MatchStarted,
    PauseDetected,
    NormalResultCandidate,
    NoContestCandidate,
    UnknownState,
};

struct SkillMatchEvent
{
    SkillMatchEventType Type;
    unsigned int Frame;
    int PlayerIndex = -1;
    int WinnerIndex = -1;
    int LoserIndex = -1;
};

class SkillMatchGameWatcher
{
  public:
    explicit SkillMatchGameWatcher(int localPlayerIndex);

    std::vector<SkillMatchEvent> ProcessSnapshot(const SkillMatchSnapshot& snapshot);

  private:
    enum class State { WaitingForMatch, Playing, PausePending, ResultPending };

    int localPlayerIndex;
    State state = State::WaitingForMatch;
    uint8_t previousStatus = 0;
    bool hasPreviousStatus = false;
    bool unknownPauseReported = false;
    bool resultReported = false;
    bool noContestReported = false;
};

#endif // CORE_SKILLMATCHGAMEWATCHER_HPP
