<p align="right">
  <a href="Logger_README.md">English</a> | <a href="Logger_README.zh-CN.md">中文</a>
</p>

# stdpp Logger — C++20 Pluggable Logging Library

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Header-only](https://img.shields.io/badge/header--only-yes-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

A single-header, zero-dependency C++20 logging library with a pluggable `LogSink` abstraction, built-in console and file sinks, RAII stream logging, and debug message boxes — all with thread-safe sink management and atomic log levels.

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [API Reference](#api-reference)
- [Custom Sinks](#custom-sinks)
- [Thread Safety](#thread-safety)
- [Tests](#tests)

## Features

- **Pluggable Sink Architecture** — `LogSink` abstract base class with `ConsoleSink` and `FileSink` built in. Register custom sinks at runtime.
- **RAII Stream Logging** — `LogMessage` accumulates via `operator<<` and commits on destruction. Zero manual flush.
- **Six Log Levels** — `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Critical` with colored console output.
- **Debug Message Boxes** — `MessageBoxHelper` pops up Windows message boxes for critical errors during development.
- **Compile-time Level Elision** — `TLOG`/`DLOG` compile to no-ops in release builds via `NullLog`.
- **Atomic Log Levels** — Each sink independently manages its threshold with `std::atomic<Level>`.
- **Header-only** — `#include "logger.hpp"` is all you need (requires `<windows.h>`).

## Quick Start

```cpp
#include "logger.hpp"

int main() {
    // Default: ConsoleSink at Trace level — just works.
    ELOG << "Something went wrong: " << e.what();

    // Add file logging
    stdpp::log::prepare_file_logging("logs/");
    WLOG << "This goes to both console and file";

    // Debug-only logging (compiled away in Release)
    DLOG << "Debug info: x = " << x;

    // Message box for critical errors (Debug only)
    EMSG(ELOG) << "Fatal error — check logs";
    // ^ Pops up a message box AND writes to ELOG stream
}
```

## Architecture

```
Logger (namespace stdpp::log)
  ├── log() dispatches to all registered sinks
  ├── LogSink (abstract base)
  │     ├── ConsoleSink — ANSI-colored std::print output
  │     ├── FileSink   — timestamped file output
  │     └── [Custom]   — user-defined sinks
  ├── LogMessage — RAII stream builder → calls log() on destruction
  ├── MessageBoxHelper — debug popup + optional log duplication
  ├── NullLog — no-op sink for compile-time elision
  └── ConsoleManager — Windows console setup (VT processing, encoding)
```

## API Reference

### Namespace & Types

```cpp
namespace stdpp::log {
    enum class Level : std::uint8_t { None, Trace, Debug, Info, Warning, Error, Critical };
```

### Logging Macros

| Macro | Level | Release Behavior |
|-------|-------|------------------|
| `CLOG` | Critical | Active |
| `ELOG` | Error | Active |
| `WLOG` | Warning | Active |
| `ILOG` | Info | Active |
| `TLOG` | Trace | `NullLog` (no-op) |
| `DLOG` | Debug | `NullLog` (no-op) |

```cpp
ELOG << "Connection failed: " << err;
WLOG << "Retrying in " << delay << "ms";
```

### Message Box Macros

| Macro | Level | Release Behavior |
|-------|-------|------------------|
| `CMSG` / `EMSG` / `WMSG` / `IMSG` | C/E/W/I | `NullLog` (no-op) |
| `DMSG` / `TMSG` | D/T | `NullLog` (no-op) |

`EMSG(log_macro)` shows a message box AND duplicates output to `log_macro`:

```cpp
EMSG(ELOG) << "Fatal error";  // popup + ELOG stream
```

### Sink Management

```cpp
// Register a custom sink (thread-safe)
stdpp::log::add_sink(std::make_unique<MyCustomSink>());

// Remove all sinks
stdpp::log::clear_sinks();

// Prepare file logging (replaces previous FileSink)
stdpp::log::prepare_file_logging("logs/");
```

### LogSink Interface

```cpp
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual auto write(Level level, std::string_view message) -> void = 0;
    auto set_level(Level lvl) -> void;
    [[nodiscard]] auto get_level() const -> Level;
protected:
    std::atomic<Level> level_{Level::Trace};
};
```

### ConsoleManager

```cpp
stdpp::log::ConsoleManager::open_console();   // Alloc/attach console window
stdpp::log::ConsoleManager::clear_console();  // Clear console screen
stdpp::log::ConsoleManager::set_io_fast();    // Disable stdio buffering
```

## Custom Sinks

```cpp
class JsonSink final : public stdpp::log::LogSink {
public:
    explicit JsonSink(std::filesystem::path path) : file_(path, std::ios::app) {
        set_level(stdpp::log::Level::Warning);
    }

    auto write(stdpp::log::Level level, std::string_view msg) -> void override {
        if (!file_.is_open()) return;
        file_ << std::format(R"({{"level":"{}","msg":"{}"}})", get_level_text(level), msg) << '\n';
    }

private:
    std::ofstream file_;
};

// Register it
stdpp::log::add_sink(std::make_unique<JsonSink>("app.json"));
// Now WLOG, ELOG, CLOG will also write JSON output
```

## Thread Safety

- **Sink registration** (`add_sink`, `clear_sinks`) — protected by internal mutex.
- **Sink `write()` calls** — dispatched under a shared lock. Each `FileSink` has its own mutex for ofstream access.
- **Log level changes** (`set_level`) — atomic stores, lock-free.
- **`LogMessage` stream** — single-threaded by nature (local variable). Do not share one `LogMessage` across threads.

## Tests

```cpp
// test_logger.cpp — compile: cl /std:c++20 /EHsc test_logger.cpp
#include "logger.hpp"
#include <cassert>
#include <sstream>

void test_console_sink() {
    // ConsoleSink writes to stdout (verify visually or capture)
    auto sink = std::make_unique<stdpp::log::ConsoleSink>();
    assert(sink->get_level() == stdpp::log::Level::Trace);
    sink->set_level(stdpp::log::Level::Error);
    assert(sink->get_level() == stdpp::log::Level::Error);
}

void test_file_sink() {
    auto sink = std::make_unique<stdpp::log::FileSink>("test_logs");
    assert(sink->get_level() == stdpp::log::Level::Trace);
    // Verify test_logs/logs.log is created
}

void test_log_message() {
    stdpp::log::LogMessage msg(stdpp::log::Level::Info,
                               std::source_location::current());
    msg << "Hello " << 42;
    // msg destructor calls log() — writes to registered sinks
}

void test_null_log() {
    stdpp::log::NullLog nl;
    nl << "this is " << "ignored";  // no-op, no side effects
}

int main() {
    test_console_sink();
    test_file_sink();
    test_log_message();
    test_null_log();
}
```
