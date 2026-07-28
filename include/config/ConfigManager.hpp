#pragma once
//
// ConfigManager.hpp
// Minimal INI parser/writer plus the strongly-typed MinerConfig produced
// from it. Automatically creates config.ini with sane defaults if the
// file does not exist, and validates values after loading.
//

#include <string>
#include <map>
#include <vector>
#include <filesystem>
#include <optional>

namespace cppminer::config {

/// Strongly-typed view of everything the application needs from config.ini.
/// Kept as a plain struct (aggregate) for simplicity and const-correct use
/// throughout the rest of the program.
struct MinerConfig {
    // [Pool]
    std::string poolUrl      = "stratum+tcp://pool.example.com:3333";
    std::string wallet       = "YOUR_WALLET";
    std::string worker       = "linux01";
    std::string password     = "x";

    // [Mining]
    unsigned int threads     = 0;      // 0 => auto-detect
    std::string algorithm    = "randomx";
    std::string minerType    = "cpu";  // "cpu" or "gpu"
    bool cpuAffinity         = true;
    bool hugePages           = true;
    unsigned int donateLevel = 1;      // percent, 0-100

    // [Performance]
    std::string priority     = "normal"; // low|normal|high
    unsigned int hashrateIntervalSec = 5;
    bool benchmark           = false;

    // [Logging]
    std::string logLevel     = "info";
    std::string logFile      = "miner.log";

    // [Network]
    unsigned int reconnectDelaySec = 5;
    bool keepAlive           = true;
};

/// Loads, creates, and validates the miner's INI configuration file.
class ConfigManager {
public:
    explicit ConfigManager(std::filesystem::path path);

    /// Loads config.ini, creating it with default values first if it does
    /// not already exist on disk. Returns the parsed, validated config.
    /// Throws std::runtime_error on unrecoverable I/O or validation errors.
    MinerConfig load();

    /// Loads configuration from an INI-formatted string (e.g. the config
    /// that was embedded into the binary at build time). No file I/O.
    MinerConfig loadFromString(const std::string& iniContent);

    /// Writes out the built-in default configuration to disk_.
    void writeDefaultFile() const;

    /// Path this manager reads from / writes to.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    /// Human-readable warnings produced by validation during the last load().
    [[nodiscard]] const std::vector<std::string>& lastWarnings() const noexcept { return warnings_; }

private:
    /// Parses raw INI text into a two-level map: section -> key -> value.
    static std::map<std::string, std::map<std::string, std::string>>
    parseIni(const std::string& text);

    std::filesystem::path path_;
    std::vector<std::string> warnings_;
};

} // namespace cppminer::config
