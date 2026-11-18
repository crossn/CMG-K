#include "../Source/RMG-Core/SkillMatchLocalHttp.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace std::chrono_literals;
int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 4);
    auto port = static_cast<uint16_t>(std::atoi(argv[1]));
    std::string mode = argv[2];
    const std::string eventId = argc == 4 ? argv[3] : "test-event";
    if (mode == "off") return 0; // No outbox/sender constructed.
    if (mode == "direct") {
        assert(SkillMatchPostLocal(port, "bad\r\nheader", "{}") == SkillMatchOutbox::Delivery::Reject);
        assert(SkillMatchPostLocal(port, "valid", std::string(16385, 'x')) == SkillMatchOutbox::Delivery::Reject);
        return 0;
    }
    std::atomic<int> calls{0};
    std::atomic<bool> ack{false};
    const auto start = std::chrono::steady_clock::now();
    {
        SkillMatchOutbox queue([&](const std::string& id, const std::string& payload) {
            ++calls;
            auto result = SkillMatchPostLocal(port, id, payload);
            if (result == SkillMatchOutbox::Delivery::Ack) ack = true;
            return result;
        }, 30ms, 1);
        assert(queue.Enqueue(eventId, "{\"observation\":true}") == SkillMatchOutbox::EnqueueResult::Queued);
        assert(queue.Enqueue(eventId, "{\"observation\":true}") == SkillMatchOutbox::EnqueueResult::Duplicate);
        assert(queue.Enqueue(eventId, "different") == SkillMatchOutbox::EnqueueResult::Conflict);
        assert(queue.Enqueue("other", "{}") == SkillMatchOutbox::EnqueueResult::Full);
        assert(std::chrono::steady_clock::now() - start < 500ms);
        if (mode == "disable") {
            std::this_thread::sleep_for(100ms);
            queue.Disable();
        } else {
            for (int i = 0; i < 800 && !ack && !queue.IsRejected(); ++i) std::this_thread::sleep_for(10ms);
            if (mode == "reject") assert(queue.IsRejected() && !ack);
            else assert(ack && calls >= 1);
        }
    }
    assert(std::chrono::steady_clock::now() - start < 9s);
}
