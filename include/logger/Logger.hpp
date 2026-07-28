#pragma once
//
// Logger.hpp
// Thread-safe logger with colored console output and file logging.
// No global state: the application owns one Logger instance and passes
// a reference (or shared_ptr) to every component that needs to log.
//

#include <string>
#include <mutex>
#include <fstream>
#include <chrono>
#include <memory>

namespace cppminer::logger {

/// Severity levels, ordered so that `level_ >= threshold` filtering works.
enum class Level : int {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

/// Convert a Level to its short textual tag ("DEBUG", "INFO", ...).
const char* levelToString(Level level) noexcept;

/// Parse a level name from config.ini ("debug", "info", "warn", "error").
Level levelFromString(const std::string& text) noexcept;

/// Thread-safe logger writing to stdout (colored) and to a log file.
///
/// Usage:
///     Logger log("miner.log", Level::Info);
///     log.info("Started mining on {}", poolUrl); // simple {} substitution
///
/// The class is deliberately simple (no external formatting library
/// dependency): it supports a single "{}" placeholder style via a small
/// variadic helper, falling back to plain concatenation.
class Logger {
public:
    /// Opens (append mode) the given log file and sets the minimum level
    /// that will be emitted. Throws std::runtime_error if the file cannot
    /// be opened.
    Logger(std::string filePath, Level minLevel);

    ~Logger() = default;

    // Non-copyable (owns a std::ofstream + mutex), movable is also disabled
    // for simplicity since the app creates exactly one instance.
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Change the minimum severity level at runtime.
    void setLevel(Level level) noexcept;

    /// Core logging entry points.
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

    /// Generic log function used internally by the level helpers above.
    void log(Level level, const std::string& message);

private:
    std::string timestamp() const;

    std::mutex mutex_;              ///< Guards console + file writes.
    std::ofstream file_;            ///< Log file stream (append mode).
    Level minLevel_;                ///< Minimum level that gets emitted.
};

} // namespace cppminer::logger
