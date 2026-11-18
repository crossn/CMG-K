#include "SkillMatchOutbox.hpp"

#include <utility>

SkillMatchOutbox::SkillMatchOutbox(Sender send, std::chrono::milliseconds delay, size_t limit)
    : sender(std::move(send)), retryDelay(delay), capacity(limit), worker(&SkillMatchOutbox::Run, this)
{
}

SkillMatchOutbox::~SkillMatchOutbox()
{
    Disable();
    worker.join();
}

SkillMatchOutbox::EnqueueResult SkillMatchOutbox::Enqueue(const std::string& id, const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!enabled) return EnqueueResult::Disabled;
    const auto existing = seen.find(id);
    if (existing != seen.end())
        return existing->second == payload ? EnqueueResult::Duplicate : EnqueueResult::Conflict;
    if (id.empty() || payload.empty() || seen.size() >= capacity) return EnqueueResult::Full;
    seen.emplace(id, payload);
    pending.push_back(id);
    changed.notify_one();
    return EnqueueResult::Queued;
}

void SkillMatchOutbox::Disable()
{
    std::lock_guard<std::mutex> lock(mutex);
    enabled = false;
    pending.clear();
    changed.notify_all();
}

void SkillMatchOutbox::Run()
{
    std::unique_lock<std::mutex> lock(mutex);
    while (enabled)
    {
        changed.wait(lock, [this] { return !enabled || !pending.empty(); });
        if (!enabled) break;
        const auto id = pending.front();
        const auto payload = seen.at(id);
        lock.unlock();
        bool accepted = false;
        try { accepted = sender(id, payload); } catch (...) { /* retry */ }
        lock.lock();
        if (!enabled) break;
        if (accepted) pending.pop_front();
        else changed.wait_for(lock, retryDelay, [this] { return !enabled; });
    }
}
