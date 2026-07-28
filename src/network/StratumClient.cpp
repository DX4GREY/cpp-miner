#include "network/StratumClient.hpp"

#include <sstream>
#include <cstring>
#include <chrono>
#include <cerrno>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

namespace cppminer::network {

using cppminer::utils::json::Value;

StratumClient::StratumClient(std::string poolUrl,
                              std::string wallet,
                              std::string worker,
                              std::string password,
                              unsigned int reconnectDelaySec,
                              bool keepAlive,
                              logger::Logger& logger)
    : poolUrl_(std::move(poolUrl)),
      wallet_(std::move(wallet)),
      worker_(std::move(worker)),
      password_(std::move(password)),
      reconnectDelaySec_(reconnectDelaySec),
      keepAlive_(keepAlive),
      logger_(logger) {}

StratumClient::~StratumClient() {
    stop();
    closeInterruptPipe();
}

bool StratumClient::createInterruptPipe() {
    if (::pipe(interruptFds_) != 0) {
        logger_.error("Stratum: failed to create interrupt pipe: " + std::string(std::strerror(errno)));
        return false;
    }
    // Make read end non-blocking so we can drain it
    int flags = ::fcntl(interruptFds_[0], F_GETFL, 0);
    ::fcntl(interruptFds_[0], F_SETFL, flags | O_NONBLOCK);
    return true;
}

void StratumClient::closeInterruptPipe() {
    if (interruptFds_[0] >= 0) { ::close(interruptFds_[0]); interruptFds_[0] = -1; }
    if (interruptFds_[1] >= 0) { ::close(interruptFds_[1]); interruptFds_[1] = -1; }
}

StratumClient::ParsedUrl StratumClient::parseUrl(const std::string& url) {
    // Accepts "stratum+tcp://host:port" or plain "host:port".
    std::string rest = url;
    const auto schemePos = rest.find("://");
    if (schemePos != std::string::npos) {
        rest = rest.substr(schemePos + 3);
    }
    const auto colonPos = rest.find(':');
    ParsedUrl parsed;
    if (colonPos == std::string::npos) {
        parsed.host = rest;
        parsed.port = "3333"; // sane fallback
    } else {
        parsed.host = rest.substr(0, colonPos);
        parsed.port = rest.substr(colonPos + 1);
    }
    return parsed;
}

void StratumClient::start() {
    if (running_.exchange(true)) {
        return; // already running
    }
    closeInterruptPipe();
    createInterruptPipe();
    thread_ = std::thread(&StratumClient::runLoop, this);
}

void StratumClient::stop() {
    if (!running_.exchange(false)) {
        return; // already stopped
    }
    // Write a byte to the interrupt pipe to wake up poll()/connect()
    if (interruptFds_[1] >= 0) {
        char c = 1;
        ::write(interruptFds_[1], &c, 1);
    }
    disconnect();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void StratumClient::disconnect() noexcept {
    if (socketFd_ >= 0) {
        ::shutdown(socketFd_, SHUT_RDWR);
        ::close(socketFd_);
        socketFd_ = -1;
    }
    authorized_.store(false);
}

bool StratumClient::connectOnce() {
    const ParsedUrl parsed = parseUrl(poolUrl_);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* resolved = nullptr;
    const int rc = getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &resolved);
    if (rc != 0 || resolved == nullptr) {
        logger_.error("Stratum: DNS resolution failed for " + parsed.host + ": " + gai_strerror(rc));
        return false;
    }

    int fd = -1;
    for (addrinfo* it = resolved; it != nullptr; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype | SOCK_NONBLOCK, it->ai_protocol);
        if (fd < 0) continue;

        // Non-blocking connect with timeout
        if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break; // immediate success
        }
        if (errno != EINPROGRESS) {
            ::close(fd);
            fd = -1;
            continue;
        }

        // Wait for connection to complete with timeout, interruptible via pipe
        pollfd pfds[2];
        pfds[kShutdownFdIndex].fd = interruptFds_[0];
        pfds[kShutdownFdIndex].events = POLLIN;
        pfds[kSocketFdIndex].fd = fd;
        pfds[kSocketFdIndex].events = POLLOUT;

        const int pollRc = ::poll(pfds, 2, 5000); // 5 second connect timeout
        if (pollRc <= 0) {
            // Timeout or error
            ::close(fd);
            fd = -1;
            continue;
        }
        if (pfds[kShutdownFdIndex].revents & POLLIN) {
            // Shutdown requested during connect
            ::close(fd);
            fd = -1;
            break;
        }
        if (pfds[kSocketFdIndex].revents & POLLOUT) {
            // Connection established; check SO_ERROR
            int soError = 0;
            socklen_t len = sizeof(soError);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) != 0 || soError != 0) {
                ::close(fd);
                fd = -1;
                continue;
            }
            break; // success
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(resolved);

    if (fd < 0) {
        logger_.error("Stratum: unable to connect to " + parsed.host + ":" + parsed.port);
        return false;
    }

    // Restore blocking mode for the socket (simplifies send/recv)
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    socketFd_ = fd;
    logger_.info("Stratum: connected to " + parsed.host + ":" + parsed.port);

    // -- mining.subscribe --
    {
        Value params = Value::makeArray();
        params.push(Value::makeString("cpp-miner/1.0"));
        Value req = Value::makeObject();
        req.set("id", Value::makeNumber(1));
        req.set("method", Value::makeString("mining.subscribe"));
        req.set("params", params);
        if (!sendLine(req.dump())) return false;
    }

    // -- mining.authorize --
    {
        Value params = Value::makeArray();
        params.push(Value::makeString(wallet_ + "." + worker_));
        params.push(Value::makeString(password_));
        Value req = Value::makeObject();
        req.set("id", Value::makeNumber(2));
        req.set("method", Value::makeString("mining.authorize"));
        req.set("params", params);
        if (!sendLine(req.dump())) return false;
    }

    return true;
}

bool StratumClient::sendLine(const std::string& jsonLine) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (socketFd_ < 0) return false;
    const std::string withNewline = jsonLine + "\n";
    std::size_t sent = 0;
    while (sent < withNewline.size()) {
        const ssize_t n = ::send(socketFd_, withNewline.data() + sent,
                                  withNewline.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void StratumClient::submitShare(const std::string& jobId,
                                 const std::string& extranonce2,
                                 const std::string& ntime,
                                 const std::string& nonceHex) {
    Value params = Value::makeArray();
    params.push(Value::makeString(wallet_ + "." + worker_));
    params.push(Value::makeString(jobId));
    params.push(Value::makeString(extranonce2));
    params.push(Value::makeString(ntime));
    params.push(Value::makeString(nonceHex));

    int requestId = 0;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        requestId = ++nextRequestId_;
    }

    Value req = Value::makeObject();
    req.set("id", Value::makeNumber(requestId));
    req.set("method", Value::makeString("mining.submit"));
    req.set("params", params);

    if (!sendLine(req.dump())) {
        logger_.warn("Stratum: failed to submit share (socket write error).");
    }
}

void StratumClient::handleNotify(const Value& params) {
    // Log the actual params structure so we can see the format
    logger_.info("Stratum: mining.notify type=" +
                 std::to_string(static_cast<int>(params.type())) +
                 " size=" + (params.type() == Value::Type::Array ?
                   std::to_string(params.asArray().size()) : "N/A") +
                 " data=" + params.dump().substr(0, 300));

    // MoneroOcean and other XMR pools send params as an array with >= 9 elements
    if (params.type() == Value::Type::Array && params.asArray().size() >= 9) {
        const auto& arr = params.asArray();

        miner::MiningJob job;
        job.jobId     = arr[0].asString();
        job.prevHash  = arr[1].asString();
        job.coinbase1 = arr[2].asString();
        job.coinbase2 = arr[3].asString();
        if (arr[4].type() == Value::Type::Array) {
            for (const auto& branch : arr[4].asArray()) {
                job.merkleBranches.push_back(branch.asString());
            }
        }
        job.version    = arr[5].asString();
        job.bits       = arr[6].asString();
        job.time       = arr[7].asString();
        job.cleanJobs  = (arr.size() > 8 && arr[8].type() == Value::Type::Bool) ? arr[8].asBool() : false;
        job.extranonce1 = extranonce1_;
        job.extranonce2Size = extranonce2Size_;

        currentJobId_ = job.jobId;
        logger_.info("Stratum: received job " + job.jobId + " (clean=" +
                     (job.cleanJobs ? "true" : "false") + ")");
        if (onJob_) onJob_(job);
        return;
    }

    logger_.warn("Stratum: unexpected mining.notify format, ignoring.");
}

void StratumClient::handleSetDifficulty(const Value& params) {
    if (params.type() != Value::Type::Array || params.asArray().empty()) return;
    const double diff = params.asArray()[0].asNumber();
    logger_.info("Stratum: new difficulty " + std::to_string(diff));
    if (onDifficulty_) onDifficulty_(diff);
}

void StratumClient::handleResponse(const Value& msg) {
    // Responses to our own requests carry "id" and either "result" or
    // "error". We distinguish subscribe/authorize (ids 1/2) from share
    // submissions (ids >= 3) by convention established in this class.
    const auto idOpt = msg.get("id");
    if (!idOpt || idOpt->type() != Value::Type::Number) return;
    const int id = static_cast<int>(idOpt->asNumber());

    const auto resultOpt = msg.get("result");
    const bool resultTrue = resultOpt && resultOpt->type() == Value::Type::Bool && resultOpt->asBool();
    const bool hasError = msg.get("error").has_value() &&
                           msg.get("error")->type() != Value::Type::Null;

    if (id == 2) {
        // mining.authorize response.
        authorized_.store(resultTrue && !hasError);
        if (authorized_.load()) {
            logger_.info("Stratum: worker authorized.");
        } else {
            logger_.error("Stratum: authorization failed.");
        }
        return;
    }
    if (id == 1) {
        // mining.subscribe response: result = [subscriptions, extranonce1, extranonce2Size]
        if (resultOpt && resultOpt->type() == Value::Type::Array &&
            resultOpt->asArray().size() >= 3) {
            extranonce1_ = resultOpt->asArray()[1].asString();
            extranonce2Size_ = static_cast<unsigned int>(resultOpt->asArray()[2].asNumber());
            logger_.debug("Stratum: subscribed, extranonce1=" + extranonce1_);
        }
        return;
    }

    // Otherwise treat as a share submission result.
    SubmitResult result;
    result.accepted = resultTrue && !hasError;
    if (!result.accepted) {
        const auto errorOpt = msg.get("error");
        if (errorOpt && errorOpt->type() == Value::Type::Array && !errorOpt->asArray().empty()) {
            result.message = errorOpt->asArray().back().asString();
        }
    }
    if (onSubmit_) onSubmit_(result);
}

void StratumClient::handleLine(const std::string& line) {
    if (line.empty()) return;
    const auto parsed = utils::json::parse(line);
    if (!parsed) {
        logger_.debug("Stratum: failed to parse line: " + line);
        return;
    }
    const Value& msg = *parsed;

    const auto methodOpt = msg.get("method");
    if (methodOpt && methodOpt->type() == Value::Type::String) {
        const std::string& method = methodOpt->asString();
        const auto paramsOpt = msg.get("params");
        const Value params = paramsOpt ? *paramsOpt : Value::makeArray();

        if (method == "mining.notify") {
            handleNotify(params);
        } else if (method == "mining.set_difficulty") {
            handleSetDifficulty(params);
        } else {
            logger_.debug("Stratum: unhandled method " + method);
        }
        return;
    }

    // No "method" field => this is a response to one of our requests.
    handleResponse(msg);
}

void StratumClient::runLoop() {
    while (running_.load()) {
        if (!connectOnce()) {
            disconnect();
            // Wait for reconnect delay, but wake up immediately if shutdown requested
            for (unsigned int waited = 0; waited < reconnectDelaySec_ && running_.load(); ++waited) {
                // Use poll on interrupt pipe to allow immediate wakeup
                pollfd pfd;
                pfd.fd = interruptFds_[0];
                pfd.events = POLLIN;
                if (::poll(&pfd, 1, 1000) > 0 && (pfd.revents & POLLIN)) {
                    // Shutdown requested during reconnect wait
                    break;
                }
            }
            continue;
        }

        std::string lineBuffer;
        char recvBuf[4096];

        while (running_.load()) {
            pollfd pfds[2];
            pfds[kShutdownFdIndex].fd = interruptFds_[0];
            pfds[kShutdownFdIndex].events = POLLIN;
            pfds[kSocketFdIndex].fd = socketFd_;
            pfds[kSocketFdIndex].events = POLLIN;

            const int pollRc = ::poll(pfds, 2, 1000 /* ms */);
            if (pollRc < 0) {
                break; // poll error -> reconnect
            }
            if (pollRc == 0) {
                continue; // timeout, check running_ again
            }

            // Check if shutdown was requested via interrupt pipe
            if (pfds[kShutdownFdIndex].revents & POLLIN) {
                // Drain the pipe
                char buf[64];
                while (::read(interruptFds_[0], buf, sizeof(buf)) > 0) {}
                break; // exit inner loop to check running_ and reconnect
            }

            if (pfds[kSocketFdIndex].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                break;
            }

            const ssize_t n = ::recv(socketFd_, recvBuf, sizeof(recvBuf), 0);
            if (n <= 0) {
                break; // connection closed or error -> reconnect
            }
            lineBuffer.append(recvBuf, static_cast<std::size_t>(n));

            std::size_t newlinePos;
            while ((newlinePos = lineBuffer.find('\n')) != std::string::npos) {
                const std::string line = lineBuffer.substr(0, newlinePos);
                lineBuffer.erase(0, newlinePos + 1);
                handleLine(line);
            }
        }

        if (!running_.load()) break;

        logger_.warn("Stratum: connection lost, reconnecting in " +
                      std::to_string(reconnectDelaySec_) + "s...");
        disconnect();
        for (unsigned int waited = 0; waited < reconnectDelaySec_ && running_.load(); ++waited) {
            pollfd pfd;
            pfd.fd = interruptFds_[0];
            pfd.events = POLLIN;
            if (::poll(&pfd, 1, 1000) > 0 && (pfd.revents & POLLIN)) {
                break;
            }
        }
    }
}

} // namespace cppminer::network