#include "config/ConfigManager.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace cppminer::config {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool toBool(const std::string& s, bool fallback) {
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return fallback;
}

unsigned int toUInt(const std::string& s, unsigned int fallback) {
    try {
        return static_cast<unsigned int>(std::stoul(s));
    } catch (...) {
        return fallback;
    }
}

/// Validates and clamps a MinerConfig, returning human-readable warnings.
std::vector<std::string> validate(MinerConfig& cfg) {
    std::vector<std::string> warnings;

    if (cfg.wallet.empty() || cfg.wallet == "YOUR_WALLET") {
        warnings.push_back("Pool.wallet is not set; using placeholder wallet address.");
    }
    if (cfg.donateLevel > 100) {
        warnings.push_back("Mining.donate_level > 100, clamping to 100.");
        cfg.donateLevel = 100;
    }
    if (cfg.priority != "low" && cfg.priority != "normal" && cfg.priority != "high") {
        warnings.push_back("Performance.priority invalid, defaulting to 'normal'.");
        cfg.priority = "normal";
    }
    if (cfg.minerType != "cpu" && cfg.minerType != "gpu") {
        warnings.push_back("Mining.miner_type invalid ('" + cfg.minerType + "'), defaulting to 'cpu'.");
        cfg.minerType = "cpu";
    }
    if (cfg.hashrateIntervalSec == 0) {
        warnings.push_back("Performance.print_hashrate_interval must be >0, using 5.");
        cfg.hashrateIntervalSec = 5;
    }
    if (cfg.reconnectDelaySec == 0) {
        warnings.push_back("Network.reconnect_delay must be >0, using 5.");
        cfg.reconnectDelaySec = 5;
    }
    return warnings;
}

/// Shared implementation: parses a parsed INI map into a MinerConfig.
MinerConfig parseSections(
    const std::map<std::string, std::map<std::string, std::string>>& sections,
    std::vector<std::string>& warnings) {

    MinerConfig cfg;

    auto section = [&sections](const std::string& name) -> const std::map<std::string, std::string>* {
        const auto it = sections.find(name);
        return it == sections.end() ? nullptr : &it->second;
    };
    auto get = [](const std::map<std::string, std::string>* sec,
                  const std::string& key, const std::string& fallback) -> std::string {
        if (!sec) return fallback;
        const auto it = sec->find(key);
        return it == sec->end() ? fallback : it->second;
    };

    if (const auto* pool = section("Pool")) {
        cfg.poolUrl  = get(pool, "url", cfg.poolUrl);
        cfg.wallet   = get(pool, "wallet", cfg.wallet);
        cfg.worker   = get(pool, "worker", cfg.worker);
        cfg.password = get(pool, "password", cfg.password);
    }
    if (const auto* mining = section("Mining")) {
        cfg.threads     = toUInt(get(mining, "threads", "0"), 0);
        cfg.algorithm   = get(mining, "algorithm", cfg.algorithm);
        cfg.minerType   = get(mining, "miner_type", cfg.minerType);
        cfg.cpuAffinity = toBool(get(mining, "cpu_affinity", "true"), true);
        cfg.hugePages   = toBool(get(mining, "huge_pages", "true"), true);
        cfg.donateLevel = toUInt(get(mining, "donate_level", "1"), 1);
    }
    if (const auto* perf = section("Performance")) {
        cfg.priority           = get(perf, "priority", cfg.priority);
        cfg.hashrateIntervalSec = toUInt(get(perf, "print_hashrate_interval", "5"), 5);
        cfg.benchmark          = toBool(get(perf, "benchmark", "false"), false);
    }
    if (const auto* log = section("Logging")) {
        cfg.logLevel = get(log, "level", cfg.logLevel);
        cfg.logFile  = get(log, "log_file", cfg.logFile);
    }
    if (const auto* net = section("Network")) {
        cfg.reconnectDelaySec = toUInt(get(net, "reconnect_delay", "5"), 5);
        cfg.keepAlive          = toBool(get(net, "keep_alive", "true"), true);
    }

    warnings = validate(cfg);
    return cfg;
}

} // namespace

ConfigManager::ConfigManager(std::filesystem::path path) : path_(std::move(path)) {}

void ConfigManager::writeDefaultFile() const {
    static constexpr const char* kDefaultIni = R"INI(; ------------------------------------------------------------------
; Miner configuration file. Auto-generated with default values.
; Edit the values below, then restart the miner.
; ------------------------------------------------------------------

[Pool]
url=stratum+tcp://pool.example.com:3333
wallet=YOUR_WALLET
worker=linux01
password=x

[Mining]
threads=0
algorithm=randomx
miner_type=cpu
cpu_affinity=true
huge_pages=true
donate_level=1

[Performance]
priority=normal
print_hashrate_interval=5
benchmark=false

[Logging]
level=info
log_file=miner.log

[Network]
reconnect_delay=5
keep_alive=true
)INI";

    std::ofstream out(path_, std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("ConfigManager: failed to create " + path_.string());
    }
    out << kDefaultIni;
}

std::map<std::string, std::map<std::string, std::string>>
ConfigManager::parseIni(const std::string& text) {
    std::map<std::string, std::map<std::string, std::string>> result;
    std::istringstream stream(text);
    std::string line;
    std::string currentSection;

    while (std::getline(stream, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue; // blank line or comment
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue; // malformed line, skip rather than hard-fail
        }
        const std::string key   = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        result[currentSection][key] = value;
    }
    return result;
}


MinerConfig ConfigManager::load() {
    if (!std::filesystem::exists(path_)) {
        writeDefaultFile();
    }

    std::ifstream in(path_);
    if (!in.is_open()) {
        throw std::runtime_error("ConfigManager: failed to open " + path_.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    const auto sections = parseIni(buffer.str());
    const auto result = parseSections(sections, warnings_);
    return result;
}

MinerConfig ConfigManager::loadFromString(const std::string& iniContent) {
    const auto sections = parseIni(iniContent);
    const auto result = parseSections(sections, warnings_);
    return result;
}

} // namespace cppminer::config