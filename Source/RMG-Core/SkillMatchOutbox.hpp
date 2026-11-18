/* Skill Match transport foundation. Payload encoding and delivery are supplied by
 * the future shared Web contract; this class does not access the network. */
#ifndef CORE_SKILLMATCHOUTBOX_HPP
#define CORE_SKILLMATCHOUTBOX_HPP

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class SkillMatchOutbox
{
public:
    // The sender must be bounded and return true only after an idempotent receipt.
    using Sender = std::function<bool(const std::string&, const std::string&)>;
    enum class EnqueueResult { Queued, Duplicate, Conflict, Full, Disabled };

    explicit SkillMatchOutbox(Sender sender, std::chrono::milliseconds retryDelay,
                             size_t capacity = 128);
    ~SkillMatchOutbox();
    SkillMatchOutbox(const SkillMatchOutbox&) = delete;
    SkillMatchOutbox& operator=(const SkillMatchOutbox&) = delete;
    EnqueueResult Enqueue(const std::string& id, const std::string& payload);
    void Disable();

private:
    void Run();
    Sender sender;
    std::chrono::milliseconds retryDelay;
    size_t capacity;
    std::mutex mutex;
    std::condition_variable changed;
    bool enabled = true;
    std::deque<std::string> pending;
    // Keep receipts for this outbox lifetime so repeated observations cannot resend.
    std::unordered_map<std::string, std::string> seen;
    std::thread worker;
};
#endif
