/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 */
#include "SkillMatchGameWatcher.hpp"

namespace
{
constexpr uint8_t GameStatusGo = 1;
constexpr uint8_t GameStatusPause = 2;
constexpr uint8_t GameStatusEnd = 5;
constexpr uint8_t GameStatusBossDefeat = 6;
constexpr uint8_t GameStatusSet = 7;
constexpr uint8_t NoStocks = 0xff;
}

SkillMatchGameWatcher::SkillMatchGameWatcher(int localPlayerIndex) : localPlayerIndex(localPlayerIndex)
{
}

std::vector<SkillMatchEvent> SkillMatchGameWatcher::ProcessSnapshot(const SkillMatchSnapshot& snapshot)
{
    std::vector<SkillMatchEvent> events;
    const bool transitioned = !hasPreviousStatus || previousStatus != snapshot.GameStatus;

    if (snapshot.GameStatus == GameStatusGo &&
        (state == State::WaitingForMatch || state == State::ResultPending))
    {
        state = State::Playing;
        resultReported = false;
        noContestReported = false;
        unknownPauseReported = false;
        events.push_back({SkillMatchEventType::MatchStarted, snapshot.Frame});
    }
    else if (snapshot.GameStatus == GameStatusGo && state == State::PausePending)
    {
        state = State::Playing;
    }
    else if (state == State::Playing && transitioned && snapshot.GameStatus == GameStatusPause)
    {
        state = State::PausePending;
        if (snapshot.PausePlayer <= 1)
        {
            events.push_back({SkillMatchEventType::PauseDetected, snapshot.Frame, snapshot.PausePlayer});
        }
        else if (!unknownPauseReported)
        {
            unknownPauseReported = true;
            events.push_back({SkillMatchEventType::UnknownState, snapshot.Frame});
        }
    }
    else if (state == State::PausePending && transitioned && snapshot.GameStatus == GameStatusSet)
    {
        state = State::ResultPending;
        if (!noContestReported)
        {
            noContestReported = true;
            events.push_back({SkillMatchEventType::NoContestCandidate, snapshot.Frame});
        }
    }
    else if (state == State::Playing && transitioned &&
             (snapshot.GameStatus == GameStatusEnd || snapshot.GameStatus == GameStatusBossDefeat || snapshot.GameStatus == GameStatusSet))
    {
        state = State::ResultPending;
        if (!resultReported)
        {
            if (snapshot.P1StockRaw == NoStocks && snapshot.P2StockRaw != NoStocks)
            {
                resultReported = true;
                events.push_back({SkillMatchEventType::NormalResultCandidate, snapshot.Frame, -1, 1, 0});
            }
            else if (snapshot.P2StockRaw == NoStocks && snapshot.P1StockRaw != NoStocks)
            {
                resultReported = true;
                events.push_back({SkillMatchEventType::NormalResultCandidate, snapshot.Frame, -1, 0, 1});
            }
        }
    }

    previousStatus = snapshot.GameStatus;
    hasPreviousStatus = true;
    return events;
}
