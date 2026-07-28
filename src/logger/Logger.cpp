#include "logger/Logger.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace cppminer::logger {

namespace {
    // ANSI color codes used for console output only (never written to file).
    constexpr const char* kReset  = "\033[0m";
    constexpr const char* kGray   = "\033[90m";
    constexpr const char* kCyan   = "\033[36m";
    constexpr const char* kYellow = "\033[33m";
    constexpr const char* kRed    = "\033[31m";

    const char* colorFor(Level level) noexcept {
        switch (level) {
            case Level::Debug: return kGray;
            case Level::Info:  return kCyan;
            case Level::Warn:  return kYellow;
            case Level::Error: return kRed;
        }
        return kReset;
    }
}

const char* levelToString(Level level) noexcept {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "UNKNOWN";
}

Level levelFromString(const std::string& text) noexcept {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "debug") return Level::Debug;
    if (lower == "warn" || lower == "warning") return Level::Warn;
    if (lower == "error") return Level::Error;
    return Level::Info; // safe default
}

Logger::Logger(std::string filePath, Level minLevel)
    : minLevel_(minLevel) {
    file_.open(filePath, std::ios::app);
    if (!file_.is_open()) {
        throw std::runtime_error("Logger: unable to open log file: " + filePath);
    }
}

void Logger::setLevel(Level level) noexcept {
    minLevel_ = level;
}

std::string Logger::timestamp() const {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_r(&t, &tmBuf);

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Logger::log(Level level, const std::string& message) {
    if (static_cast<int>(level) < static_cast<int>(minLevel_)) {
        return; // filtered out by current threshold
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string ts  = timestamp();
    const std::string tag = levelToString(level);

    // Console: colored.
    std::cout << kGray << "[" << ts << "] " << kReset
               << colorFor(level) << "[" << tag << "]" << kReset
               << " " << message << "\n";

    // File: plain text, no ANSI codes.
    file_ << "[" << ts << "] [" << tag << "] " << message << "\n";
    file_.flush();
}

void Logger::debug(const std::string& message) { log(Level::Debug, message); }
void Logger::info(const std::string& message)  { log(Level::Info,  message); }
void Logger::warn(const std::string& message)  { log(Level::Warn,  message); }
void Logger::error(const std::string& message) { log(Level::Error, message); }

} // namespace cppminer::logger
