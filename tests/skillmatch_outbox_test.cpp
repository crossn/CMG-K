#include "../Source/RMG-Core/SkillMatchOutbox.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;
int main()
{
    std::atomic<int> attempts{0};
    std::atomic<bool> accepted{false};
    SkillMatchOutbox queue([&](const std::string& id, const std::string& bytes) {
        assert(id == "event-1" && bytes == "opaque");
        ++attempts;
        return accepted.load() ? SkillMatchOutbox::Delivery::Ack : SkillMatchOutbox::Delivery::Retry;
    }, 10ms, 1);
    using Result = SkillMatchOutbox::EnqueueResult;
    assert(queue.Enqueue("event-1", "opaque") == Result::Queued);
    assert(queue.Enqueue("event-1", "opaque") == Result::Duplicate);
    assert(queue.Enqueue("event-1", "changed") == Result::Conflict);
    assert(queue.Enqueue("event-2", "opaque") == Result::Full);
    for (int i = 0; i < 200 && attempts.load() < 2; ++i) std::this_thread::sleep_for(5ms);
    assert(attempts.load() >= 2);
    accepted = true;
    std::this_thread::sleep_for(100ms);
    const auto afterReceipt = attempts.load();
    std::this_thread::sleep_for(50ms);
    assert(attempts.load() == afterReceipt);
    assert(queue.Enqueue("event-1", "opaque") == Result::Duplicate);
    queue.Disable();
    assert(queue.Enqueue("event-2", "opaque") == Result::Disabled);
}
