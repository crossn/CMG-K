/* Contract-neutral in-memory outbox. Delivery is provided by the caller. */
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
    enum class Delivery { Ack, Retry, Reject };
    // Sender must be bounded; Reject stops the queue for explicit fallback.
    using Sender = std::function<Delivery(const std::string&, const std::string&)>;
    enum class EnqueueResult { Queued, Duplicate, Conflict, Full, Disabled };

    explicit SkillMatchOutbox(Sender sender, std::chrono::milliseconds retryDelay,
                             size_t capacity = 128);
    ~SkillMatchOutbox();
    SkillMatchOutbox(const SkillMatchOutbox&) = delete;
    SkillMatchOutbox& operator=(const SkillMatchOutbox&) = delete;
    EnqueueResult Enqueue(const std::string& id, const std::string& payload);
    void Disable();
    bool IsRejected();

private:
    void Run();
    Sender sender;
    std::chrono::milliseconds retryDelay;
    size_t capacity;
    std::mutex mutex;
    std::condition_variable changed;
    bool enabled = true;
    bool rejected = false;
    std::deque<std::string> pending;
    // Keep receipts for this outbox lifetime so repeated observations cannot resend.
    std::unordered_map<std::string, std::string> seen;
    std::thread worker;
};
#endif
