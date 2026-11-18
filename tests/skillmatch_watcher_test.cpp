#include "../Source/RMG-Core/SkillMatchGameWatcher.hpp"
#include <cassert>

int main()
{
    SkillMatchSnapshot s{};
    SkillMatchGameWatcher watcher(0);
    s.Frame = 1; s.GameStatus = 1; s.P1StockRaw = 2; s.P2StockRaw = 2;
    auto events = watcher.ProcessSnapshot(s);
    assert(events.size() == 1 && events[0].Type == SkillMatchEventType::MatchStarted);
    s.Frame++; s.GameStatus = 2; s.PausePlayer = 1;
    events = watcher.ProcessSnapshot(s);
    assert(events.size() == 1 && events[0].Type == SkillMatchEventType::PauseDetected && events[0].PlayerIndex == 1);
    s.Frame++; s.GameStatus = 7;
    events = watcher.ProcessSnapshot(s);
    assert(events.size() == 1 && events[0].Type == SkillMatchEventType::NoContestCandidate);
    s.Frame++; s.GameStatus = 1;
    watcher.ProcessSnapshot(s);
    s.Frame++; s.GameStatus = 5; s.P1StockRaw = 0xff;
    events = watcher.ProcessSnapshot(s);
    assert(events.size() == 1 && events[0].Type == SkillMatchEventType::NormalResultCandidate && events[0].WinnerIndex == 1);
    s.Frame++; s.GameStatus = 1; s.P1StockRaw = 2;
    watcher.ProcessSnapshot(s);
    s.Frame++; s.GameStatus = 2; s.PausePlayer = 0xff;
    events = watcher.ProcessSnapshot(s);
    assert(events.size() == 1 && events[0].Type == SkillMatchEventType::UnknownState);
}
