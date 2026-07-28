//
// main.cpp
// Application entry point: parses CLI arguments, loads configuration
// (embedded defaults, can be overridden via flags), wires together the
// logger, Stratum client, and miner engine, then runs the main
// statistics-printing loop until shutdown is requested.
//
// Portable single-binary: no external config.ini needed — all settings
// are built-in. Use --config if you want to override from a file.
//

#include <iostream>
#include <sstream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstdlib>

#include "config/ConfigManager.hpp"
#include "logger/Logger.hpp"
#include "utils/CpuInfo.hpp"
#include "network/StratumClient.hpp"
#include "miner/MinerEngine.hpp"

namespace {

/// Set by the SIGINT/SIGTERM handler; polled by the main loop so that
/// shutdown happens cleanly on the main thread rather than inside a
/// signal handler (which must remain async-signal-safe).
std::atomic<bool> g_shutdownRequested{false};

extern "C" void handleSignal(int /*signum*/) {
    g_shutdownRequested.store(true);
}

constexpr const char* kVersion = "cpp-miner 1.0.0";

void printHelp(const char* argv0) {
    std::cout <<
        "Usage: " << argv0 << " [options]\n\n"
        "Options:\n"
        "  --config <path>           Path to config.ini (overrides built-in defaults)\n"
        "\n"
        "  --url <stratum_url>       Pool URL (default: stratum+tcp://pool.example.com:3333)\n"
        "  --wallet <address>        Wallet address (required for mining)\n"
        "  --worker <name>           Worker name (default: linux01)\n"
        "  --password <pass>         Pool password (default: x)\n"
        "\n"
        "  --threads <N>             Number of mining threads (0=auto, default: 0)\n"
        "  --algorithm <name>        Mining algorithm (default: randomx)\n"
        "  --miner-type <cpu|gpu>    Miner mode (default: cpu)\n"
        "  --cpu-affinity <bool>     Pin threads to cores (default: true)\n"
        "  --huge-pages <bool>       Use huge pages (default: true)\n"
        "\n"
        "  --priority <low|normal|high>  Thread priority (default: normal)\n"
        "  --print-interval <N>      Stats print interval in seconds (default: 5)\n"
        "\n"
        "  --log-level <level>       Log level: trace|debug|info|warn|error (default: info)\n"
        "  --log-file <path>         Log file path (default: miner.log)\n"
        "\n"
        "  --reconnect-delay <N>     Reconnect delay in seconds (default: 5)\n"
        "\n"
        "  --benchmark               Run a short local benchmark and exit\n"
        "  --help                    Show this help message\n"
        "  --version                 Show version information\n";
}

/// Creates a synthetic dummy job to kick-start the miners when the
/// Stratum pool is unreachable, so the miner shows real hashrate
/// instead of 0 H/s until connectivity is restored.
/// Uses an extremely high difficulty so workers never find shares
/// (avoiding spurious "failed to submit share" warnings).
cppminer::miner::MiningJob createSyntheticJob() {
    cppminer::miner::MiningJob job;
    job.jobId = "synthetic-standby";
    job.prevHash = std::string(64, '0');
    job.coinbase1 = "synthetic-coinbase1";
    job.coinbase2 = "synthetic-coinbase2";
    job.version = "20000000";
    job.bits = "1d00ffff";
    job.time = "00000000";
    job.extranonce1 = "aabbccdd";
    job.extranonce2Size = 4;
    job.difficulty = 1.0e15; // extremely high => threshold near 0 => no shares found
    return job;
}

/// Parses CLI flags and overlays them onto the given MinerConfig.
/// Supports both individual flags and --config file loading.
cppminer::config::MinerConfig parseConfig(int argc, char** argv) {
    cppminer::config::MinerConfig cfg; // all built-in defaults

    bool loadConfigFile = false;
    std::filesystem::path configPath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto nextArg = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Error: " << arg << " requires a value.\n";
            std::exit(1);
            return "";
        };

        auto nextBool = [&]() -> bool {
            const std::string v = nextArg();
            return v == "true" || v == "1" || v == "yes" || v == "on";
        };

        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
            loadConfigFile = true;
        } else if (arg == "--url")           { cfg.poolUrl = nextArg(); }
        else if (arg == "--wallet")          { cfg.wallet = nextArg(); }
        else if (arg == "--worker")          { cfg.worker = nextArg(); }
        else if (arg == "--password")        { cfg.password = nextArg(); }
        else if (arg == "--threads")         { cfg.threads = static_cast<unsigned int>(std::stoul(nextArg())); }
        else if (arg == "--algorithm")       { cfg.algorithm = nextArg(); }
        else if (arg == "--miner-type")      { cfg.minerType = nextArg(); }
        else if (arg == "--cpu-affinity")    { cfg.cpuAffinity = nextBool(); }
        else if (arg == "--huge-pages")      { cfg.hugePages = nextBool(); }
        else if (arg == "--priority")        { cfg.priority = nextArg(); }
        else if (arg == "--print-interval")  { cfg.hashrateIntervalSec = static_cast<unsigned int>(std::stoul(nextArg())); }
        else if (arg == "--log-level")       { cfg.logLevel = nextArg(); }
        else if (arg == "--log-file")        { cfg.logFile = nextArg(); }
        else if (arg == "--reconnect-delay") { cfg.reconnectDelaySec = static_cast<unsigned int>(std::stoul(nextArg())); }
        else if (arg == "--benchmark")       { cfg.benchmark = true; }
        else if (arg == "--help" || arg == "-h") { printHelp(argv[0]); std::exit(0); }
        else if (arg == "--version" || arg == "-v") { std::cout << kVersion << "\n"; std::exit(0); }
    }

    // If --config was passed, load from file, then CLI flags override file values
    if (loadConfigFile) {
        cppminer::config::ConfigManager configManager(configPath);
        try {
            cfg = configManager.load();
        } catch (const std::exception& ex) {
            std::cerr << "Fatal: failed to load configuration: " << ex.what() << "\n";
            std::exit(1);
        }
        // Re-apply individual CLI flags to override file values
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto nextArg = [&]() -> std::string {
                if (i + 1 < argc) return argv[++i];
                return "";
            };
            if (arg == "--url")           { cfg.poolUrl = nextArg(); }
            else if (arg == "--wallet")    { cfg.wallet = nextArg(); }
            else if (arg == "--worker")    { cfg.worker = nextArg(); }
            else if (arg == "--password")  { cfg.password = nextArg(); }
            else if (arg == "--threads")   { cfg.threads = static_cast<unsigned int>(std::stoul(nextArg())); }
            else if (arg == "--algorithm") { cfg.algorithm = nextArg(); }
            else if (arg == "--miner-type") { cfg.minerType = nextArg(); }
        }
    }

    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    // ---- Configuration (embedded defaults, overridable via CLI) -----------
    const cppminer::config::MinerConfig config = parseConfig(argc, argv);

    // ---- Logger ----------------------------------------------------------
    cppminer::logger::Logger logger(config.logFile, cppminer::logger::levelFromString(config.logLevel));
    logger.info(std::string("Starting ") + kVersion);

    // ---- CPU info ----------------------------------------------------------
    const auto cpuInfo = cppminer::utils::detectCpuInfo();
    logger.info("CPU: " + cpuInfo.modelName + " (" + std::to_string(cpuInfo.logicalCores) + " logical cores)");
    logger.info("Miner type: " + config.minerType);
    cppminer::utils::applyThreadPriority(config.priority);

    // ---- Warn about missing wallet if it's still the default ---------------
    if (config.wallet.empty() || config.wallet == "YOUR_WALLET") {
        logger.warn("Wallet not set! Use --wallet <address> or edit config file.");
    }

    // ---- Signal handling -----------------------------------------------
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // ---- Benchmark mode short-circuits normal operation -----------------
    if (config.benchmark) {
        cppminer::network::StratumClient dummyClient(
            config.poolUrl, config.wallet, config.worker, config.password,
            config.reconnectDelaySec, config.keepAlive, logger);
        cppminer::miner::MinerEngine engine(config, logger, dummyClient);
        engine.runBenchmark(std::chrono::seconds(20));
        return 0;
    }

    // ---- Normal mining operation -----------------------------------------
    logger.info("Pool: " + config.poolUrl + " | Worker: " + config.worker +
                " | Algorithm: " + config.algorithm + " | Mode: " + config.minerType);

    cppminer::network::StratumClient stratumClient(
        config.poolUrl, config.wallet, config.worker, config.password,
        config.reconnectDelaySec, config.keepAlive, logger);

    cppminer::miner::MinerEngine engine(config, logger, stratumClient);

    // Publish a synthetic job immediately so workers start hashing right away
    engine.updateJob(createSyntheticJob());
    engine.updateDifficulty(1.0e15);

    // Track whether a Stratum job has ever arrived so we can log it.
    std::atomic<bool> hasRealJob{false};
    std::atomic<bool> poolConnected{false};

    stratumClient.setJobCallback([&engine, &hasRealJob](const cppminer::miner::MiningJob& job) {
        engine.updateJob(job);
        hasRealJob.store(true);
    });
    stratumClient.setDifficultyCallback([&engine](double difficulty) {
        engine.updateDifficulty(difficulty);
    });
    stratumClient.setSubmitCallback([&logger, &engine, &poolConnected](const cppminer::network::SubmitResult& result) {
        poolConnected.store(true);
        engine.recordShareResult(result.accepted);
        if (result.accepted) {
            logger.info("Share accepted.");
        } else {
            logger.warn("Share rejected" + (result.message.empty() ? "." : (": " + result.message)));
        }
    });

    stratumClient.start();
    engine.start();

    logger.info("Miner running. Press Ctrl+C to stop.");

    auto lastStatsTime = std::chrono::steady_clock::now();
    auto lastNoJobWarnTime = std::chrono::steady_clock::now();
    bool warnedNoStratumJob = false;

    while (!g_shutdownRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const auto now = std::chrono::steady_clock::now();

        // Warn if we've been running for 30s without a Stratum job
        if (!hasRealJob.load() && !warnedNoStratumJob) {
            const auto elapsedNoJob = std::chrono::duration_cast<std::chrono::seconds>(now - lastNoJobWarnTime).count();
            if (elapsedNoJob >= 30) {
                logger.warn("No Stratum job received after 30s. Mining with synthetic job until pool connects.");
                warnedNoStratumJob = true;
            }
        }

        // Once a real job arrives and pool is connected, log the transition
        if (hasRealJob.load() && warnedNoStratumJob) {
            logger.info("Stratum job received, switching to pool-assigned work.");
            warnedNoStratumJob = false;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStatsTime).count();
        if (elapsed >= static_cast<long long>(config.hashrateIntervalSec)) {
            lastStatsTime = now;
            std::ostringstream stats;
            const double currHr = engine.currentHashrate();
            const double avgHr = engine.averageHashrate();
            const bool connected = poolConnected.load() || stratumClient.isAuthorized();
            stats << "Hashrate: " << currHr << " H/s (current), "
                  << avgHr << " H/s (avg) | Shares: "
                  << engine.sharesAccepted() << " accepted, "
                  << engine.sharesRejected() << " rejected | Total hashes: "
                  << engine.totalHashes()
                  << " | Pool: " << (connected ? "connected" : "connecting");
            logger.info(stats.str());
        }
    }

    logger.info("Shutdown requested, stopping miner...");
    engine.stop();
    stratumClient.stop();
    logger.info("Miner stopped cleanly.");
    return 0;
}