# cpp-miner

A modular, lightweight C++20 Stratum mining client for x86_64 Linux, built with
clean architecture: configuration, logging, networking, hashing, and threading
each live in their own folder with a narrow interface between them.

**Fully portable single binary** — no external config file needed. All settings
are built-in and can be overridden via command-line flags.

> **Honest scope note:** this project ships a pluggable `IHashAlgorithm`
> interface and a working reference algorithm (double SHA-256 and a CryptoNight
> stub) so the entire pipeline — config → Stratum handshake → job intake →
> hashing → share submission → stats — runs end to end out of the box. It does
> **not** reimplement RandomX itself (RandomX is a ~10k-line memory-hard virtual
> machine with a JIT compiler — a dedicated library, not something bolted onto
> an app like this). See "Adding a new algorithm" below for exactly where a
> real RandomX binding plugs in.

## Features

- **Single binary** — all defaults embedded, no external config.ini required
- **CLI flags** — override pool, wallet, threads, algorithm, etc. directly
- **GPU/CPU mode** — configurable via `--miner-type cpu|gpu`
- **Synthetic job fallback** — mines immediately even if pool is unreachable
- **Instant shutdown** — interrupt pipe mechanism, stops in < 100ms
- **Multi-threaded CPU mining** — pin threads to cores, configurable thread count
- **Stratum protocol** — subscribe/authorize handshake, auto-reconnect, share submission
- **No external dependencies** — pure C++20 + POSIX (pthread, socket, poll)
- **Extensible** — add new hash algorithms via a single factory function

## Project structure

```
.
├── CMakeLists.txt
├── include/                    # public headers, mirrors src/
│   ├── config/ConfigManager.hpp
│   ├── utils/{CpuInfo,Types,MiniJson}.hpp
│   ├── network/StratumClient.hpp
│   ├── miner/{MiningJob,MinerEngine}.hpp
│   ├── hash/{IHashAlgorithm,Sha256dAlgorithm,CryptonightAlgorithm,HashAlgorithmFactory}.hpp
│   ├── threading/{JobQueue,ThreadPool}.hpp
│   └── logger/Logger.hpp
└── src/                         # implementation, one .cpp per header above
    └── main.cpp                 # entry point / wiring
```

## Building

Requirements: a C++20 compiler (GCC ≥ 10 or Clang ≥ 12), CMake ≥ 3.16, and
Linux on x86_64 (uses `pthread_setaffinity_np`, `getaddrinfo`, `poll`).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc)"
```

This produces `./build/cpp-miner`. That's it — the binary is fully portable
and needs nothing else.

## Usage

### Quick start (no config file needed)

```bash
./cpp-miner --url stratum+tcp://pool.supportxmr.com:3333 --wallet YOUR_WALLET_ADDRESS --worker rig01 --threads 16 --algorithm cryptonight
```

### Using a config file (optional)

```bash
./cpp-miner --config config.ini
```

### Benchmark mode

```bash
./cpp-miner --benchmark
```

### All command-line options

```
Usage: ./cpp-miner [options]

Options:
  --config <path>           Path to config.ini (overrides built-in defaults)

  --url <stratum_url>       Pool URL (default: stratum+tcp://pool.example.com:3333)
  --wallet <address>        Wallet address (required for mining)
  --worker <name>           Worker name (default: linux01)
  --password <pass>         Pool password (default: x)

  --threads <N>             Number of mining threads (0=auto, default: 0)
  --algorithm <name>        Mining algorithm (default: randomx)
  --miner-type <cpu|gpu>    Miner mode (default: cpu)
  --cpu-affinity <bool>     Pin threads to cores (default: true)
  --huge-pages <bool>       Use huge pages (default: true)

  --priority <low|normal|high>  Thread priority (default: normal)
  --print-interval <N>      Stats print interval in seconds (default: 5)

  --log-level <level>       Log level: trace|debug|info|warn|error (default: info)
  --log-file <path>         Log file path (default: miner.log)

  --reconnect-delay <N>     Reconnect delay in seconds (default: 5)

  --benchmark               Run a short local benchmark and exit
  --help                    Show this help message
  --version                 Show version information
```

### Supported algorithms

- `randomx` — reference SHA-256d (placeholder until RandomX library is linked)
- `cryptonight` — CryptoNight stub (placeholder; attach a real CryptoNight or RandomX binding for production use)

### Example execution

```bash
./cpp-miner --url stratum+tcp://gulf.moneroocean.stream:10004 --wallet 47N28q6ZsrCR6nq7bc5hpwE12pT4FYzyh8vrnrewJgsJ9hur1yR3coRDaVf2Zo5rhWhpJV5oiPhJcJ7Sz4wuTR6R7SeSd47 --worker rig01 --algorithm cryptonight --threads 16
```

```
[2026-07-30 15:33:12] [INFO] Starting cpp-miner 1.0.0
[2026-07-30 15:33:12] [INFO] CPU: AMD Ryzen 7 7435HS (16 logical cores)
[2026-07-30 15:33:12] [INFO] Miner type: cpu
[2026-07-30 15:33:12] [INFO] Pool: stratum+tcp://gulf.moneroocean.stream:10004 | Worker: rig01 | Algorithm: cryptonight | Mode: cpu
[2026-07-30 15:33:12] [INFO] MinerEngine: starting 16 worker thread(s).
[2026-07-30 15:33:12] [INFO] Miner running. Press Ctrl+C to stop.
[2026-07-30 15:33:12] [INFO] Stratum: connected to gulf.moneroocean.stream:10004
[2026-07-30 15:33:12] [INFO] Stratum: worker authorized.
[2026-07-30 15:33:12] [INFO] Stratum: mining.notify type=4 size=7 data=["7c66","2392b2341c96c48d97a50796210ba4c987a2c69ceb9371c0d6d804d66bab82c0","9f74597b221073b1cafdc13f729b72f9c6554092b69e93a4eeca23f3024734ca","00000017b9fb0d93c16800000000000000000000000000000000000000000000",true,4475922,"1b050e83"]
[2026-07-30 15:33:12] [INFO] Stratum: received MoneroOcean job 7c66 blob=2392b2341c96c48d97a50796210ba4c987a2c69ceb9371c0d6d804d66bab82c0...
[2026-07-30 15:33:17] [INFO] Hashrate: 77.5785 H/s (current), 77.5785 H/s (avg) | Shares: 0 accepted, 0 rejected | Total hashes: 391 | Pool: connected
```

### Config file (optional)

If you prefer a config file, create `config.ini`:

```ini
[Pool]
url=stratum+tcp://gulf.moneroocean.stream:10004
wallet=47N28q6ZsrCR6nq7bc5hpwE12pT4FYzyh8vrnrewJgsJ9hur1yR3coRDaVf2Zo5rhWhpJV5oiPhJcJ7Sz4wuTR6R7SeSd47
worker=rig01
password=x

[Mining]
threads=16
algorithm=cryptonight
miner_type=cpu
cpu_affinity=true
huge_pages=false
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
```

> ⚠️ **Security:** `config.ini` contains your wallet address and pool password.
> It is listed in `.gitignore` so it will NOT be committed to git.

## Threading model

- **Worker threads (mining):** `MinerEngine::start()` spawns one
  `std::thread` per configured mining thread (or
  `std::thread::hardware_concurrency()` if `threads=0`). Each worker:
  - optionally pins itself to a CPU core via `pthread_setaffinity_np`
    (`cpu_affinity=true`),
  - constructs its **own** `IHashAlgorithm` instance (algorithms are not
    required to be thread-safe internally — one instance per thread avoids
    that problem entirely),
  - loops: read the current job from `JobQueue`, hash a batch of nonces,
    and submit any share whose digest clears the current target.

- **Network thread:** `StratumClient::start()` spawns exactly one thread
  that owns the raw TCP socket, blocks in `poll()`/`recv()`, and dispatches
  parsed JSON-RPC lines to callbacks. The `poll()` also monitors an interrupt
  pipe, enabling instant shutdown even during connection attempts.

- **Synthetic job fallback:** If the pool is unreachable, a synthetic dummy
  job with extremely high difficulty is published immediately, so workers
  start hashing and showing real hashrate right away. When the pool connects,
  the real job replaces the synthetic one automatically.

- **Shutdown:** `SIGINT`/`SIGTERM` set an `std::atomic<bool>` flag from the
  signal handler; the main loop polls that flag and initiates a clean ordered
  shutdown. An interrupt pipe wakes the network thread's `poll()` so the
  entire process stops in under 100ms.

## Class reference

| Class | Responsibility |
|---|---|
| `config::ConfigManager` / `MinerConfig` | INI parsing, default-file creation, validation |
| `logger::Logger` | Thread-safe leveled logging to console (colored) + file |
| `utils::CpuInfo` (free functions) | Core count / model detection, affinity pinning, priority |
| `utils::json::Value` / `parse()` | Minimal JSON reader/writer for Stratum messages |
| `utils::Counters` | Atomic hash/share counters shared across threads |
| `miner::MiningJob` | Plain-data Stratum job (prevhash, coinbase parts, extranonce, etc.) |
| `hash::IHashAlgorithm` | Abstract per-thread hashing interface |
| `hash::Sha256dAlgorithm` | Reference double-SHA256 implementation of that interface |
| `hash::CryptonightAlgorithm` | CryptoNight stub implementation (placeholder) |
| `hash::createAlgorithm()` | Factory mapping a config name to an `IHashAlgorithm` |
| `threading::JobQueue` | Thread-safe "current job" slot with a version counter |
| `threading::ThreadPool` | General-purpose task queue + worker threads |
| `network::StratumClient` | TCP socket, subscribe/authorize, job/difficulty callbacks, auto-reconnect |
| `miner::MinerEngine` | Owns worker threads, hashing loop, benchmark mode, hashrate/share stats |

## Adding a new mining algorithm

1. Create `include/hash/YourAlgorithm.hpp` / `src/hash/YourAlgorithm.cpp`
   implementing `hash::IHashAlgorithm` (`name()`, `initialize()`, `hash()`).
2. Register it in `hash::createAlgorithm()`
   (`src/hash/HashAlgorithmFactory.cpp`):
   ```cpp
   if (lower == "your-algo") {
       return std::make_unique<YourAlgorithm>();
   }
   ```
3. Add the new `.cpp` file to the `SOURCES` list in `CMakeLists.txt`.
4. Run with `--algorithm your-algo`.

Nothing else in the codebase needs to change — `MinerEngine` constructs one
`IHashAlgorithm` per worker thread purely through the factory.

For a **real RandomX** binding specifically: link against
[tevador/RandomX](https://github.com/tevador/RandomX) (`librandomx`), wrap
`randomx_alloc_cache` / `randomx_create_vm` / `randomx_calculate_hash` inside
a class implementing `IHashAlgorithm`, and register it as shown above.

## License

MIT