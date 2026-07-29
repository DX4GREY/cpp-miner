#pragma once
//
// StratumClient.hpp
// Asynchronous Stratum mining protocol client over a raw TCP socket.
// Handles subscribe/authorize handshake, incoming job notifications,
// share submission, and automatic reconnection with backoff.
//
// "Asynchronous" here means: the caller's thread is never blocked on
// network I/O. A dedicated background thread owns the socket and blocks
// on recv(); all events are delivered to the rest of the app via
// callbacks invoked from that thread. Callbacks must therefore be
// cheap/thread-safe (in this project they just update atomics or push
// into the thread-safe JobQueue).
//

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <optional>

#include "miner/MiningJob.hpp"
#include "logger/Logger.hpp"
#include "utils/MiniJson.hpp"

namespace cppminer::network {

/// Result of a submitted share, delivered asynchronously once the pool
/// responds to a mining.submit call.
struct SubmitResult {
    bool accepted = false;
    std::string message; // pool-provided reason on rejection, if any
};

/// Callback invoked whenever the pool pushes a new job (mining.notify).
using JobCallback = std::function<void(const miner::MiningJob&)>;

/// Callback invoked when the pool changes the target difficulty.
using DifficultyCallback = std::function<void(double)>;

/// Callback invoked once per submitted share with accept/reject status.
using SubmitCallback = std::function<void(const SubmitResult&)>;

/// Manages one connection to a Stratum pool, including reconnection.
class StratumClient {
public:
    StratumClient(std::string poolUrl,
                  std::string wallet,
                  std::string worker,
                  std::string password,
                  unsigned int reconnectDelaySec,
                  bool keepAlive,
                  logger::Logger& logger);

    ~StratumClient();

    StratumClient(const StratumClient&) = delete;
    StratumClient& operator=(const StratumClient&) = delete;

    /// Registers callbacks. Must be called before start().
    void setJobCallback(JobCallback cb) { onJob_ = std::move(cb); }
    void setDifficultyCallback(DifficultyCallback cb) { onDifficulty_ = std::move(cb); }
    void setSubmitCallback(SubmitCallback cb) { onSubmit_ = std::move(cb); }

    /// Starts the background connection thread. Returns immediately.
    void start();

    /// Signals the background thread to stop and joins it. Safe to call
    /// multiple times.
    void stop();

    /// Submits a share for the given job. Thread-safe: may be called
    /// concurrently by multiple mining worker threads.
    void submitShare(const std::string& jobId,
                      const std::string& extranonce2,
                      const std::string& ntime,
                      const std::string& nonceHex);

    void submitBlobShare(const std::string& jobId,
                         const std::string& workBlob);

    /// True once subscribe+authorize has completed successfully.
    [[nodiscard]] bool isAuthorized() const noexcept { return authorized_.load(); }

private:
    void runLoop();                 ///< Background thread entry point.
    bool connectOnce();             ///< One connection attempt; returns success.
    void disconnect() noexcept;
    bool sendLine(const std::string& jsonLine);   ///< Thread-safe socket write.
    void handleLine(const std::string& line);      ///< Dispatch one JSON-RPC line.
    void handleNotify(const utils::json::Value& params);
    void handleSetDifficulty(const utils::json::Value& params);
    void handleResponse(const utils::json::Value& msg);

    struct ParsedUrl {
        std::string host;
        std::string port;
    };
    static ParsedUrl parseUrl(const std::string& url);

    // Internal notification fd used to interrupt blocking connect()/poll()
    static constexpr int kShutdownFdIndex = 0;
    static constexpr int kSocketFdIndex = 1;
    int interruptFds_[2] = {-1, -1}; // shutdown pipe: [read, write]
    bool createInterruptPipe();
    void closeInterruptPipe();

    std::string poolUrl_;
    std::string wallet_;
    std::string worker_;
    std::string password_;
    unsigned int reconnectDelaySec_;
    bool keepAlive_;
    logger::Logger& logger_;

    int socketFd_ = -1;
    std::mutex writeMutex_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> authorized_{false};

    // Extranonce info returned by mining.subscribe, needed to build
    // correct share submissions.
    std::string extranonce1_;
    unsigned int extranonce2Size_ = 4;
    std::string currentJobId_;

    // Pending request id -> kind, so we can interpret responses that
    // only carry a numeric id and a result/error field.
    std::mutex pendingMutex_;
    int nextRequestId_ = 1;
    std::string pendingSubmitId_; // request id of the in-flight submit (string form)

    JobCallback onJob_;
    DifficultyCallback onDifficulty_;
    SubmitCallback onSubmit_;
};

} // namespace cppminer::network