<p align="right">
  <a href="Logger_README.md">English</a> | <a href="Logger_README.zh-CN.md">中文</a>
</p>

# stdpp Logger — C++20 可插拔日志库

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Header-only](https://img.shields.io/badge/header--only-yes-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

单头文件、零依赖的 C++20 日志库。提供可插拔的 `LogSink` 抽象层，内置控制台和文件输出，RAII 流式日志，以及调试用消息弹框——全部具备线程安全的 sink 管理和原子日志级别。

## 目录

- [特性](#特性)
- [快速开始](#快速开始)
- [架构](#架构)
- [API 参考](#api-参考)
- [自定义 Sink](#自定义-sink)
- [线程安全](#线程安全)
- [测试](#测试)

## 特性

- **可插拔 Sink 架构** — `LogSink` 抽象基类，内置 `ConsoleSink` 和 `FileSink`。可在运行时注册自定义 sink。
- **RAII 流式日志** — `LogMessage` 通过 `operator<<` 累积内容，析构时自动提交。无需手动刷新。
- **六级日志等级** — `Trace`、`Debug`、`Info`、`Warning`、`Error`、`Critical`，控制台输出带 ANSI 彩色。
- **调试消息弹框** — `MessageBoxHelper` 在开发阶段为严重错误弹出 Windows 消息框。
- **编译期等级裁剪** — `TLOG`/`DLOG` 在 Release 构建中编译为空操作（通过 `NullLog` 实现）。
- **原子日志等级** — 每个 sink 独立管理自己的阈值，使用 `std::atomic<Level>`。
- **仅头文件** — `#include "logger.hpp"` 即用（需要 `<windows.h>`）。

## 快速开始

```cpp
#include "logger.hpp"

int main() {
    // 默认：ConsoleSink 在 Trace 级别 — 开箱即用。
    ELOG << "出错了: " << e.what();

    // 添加文件日志
    stdpp::log::prepare_file_logging("logs/");
    WLOG << "这条日志同时输出到控制台和文件";

    // 仅 Debug 模式的日志（Release 中编译为空操作）
    DLOG << "调试信息: x = " << x;

    // 严重错误的消息弹框（仅 Debug 模式）
    EMSG(ELOG) << "致命错误 — 请检查日志";
    // ^ 弹出消息框，同时将内容写入 ELOG 流
}
```

## 架构

```
Logger（命名空间 stdpp::log）
  ├── log() 将工作分发到所有已注册的 sink
  ├── LogSink（抽象基类）
  │     ├── ConsoleSink — ANSI 彩色 std::print 输出
  │     ├── FileSink   — 带时间戳的文件输出
  │     └── [自定义]   — 用户自定义 sink
  ├── LogMessage — RAII 流构建器 → 析构时调用 log()
  ├── MessageBoxHelper — 调试弹框 + 可选的日志复制
  ├── NullLog — 编译期裁剪用的空操作 sink
  └── ConsoleManager — Windows 控制台设置（VT 处理、编码）
```

## API 参考

### 命名空间与类型

```cpp
namespace stdpp::log {
    enum class Level : std::uint8_t { None, Trace, Debug, Info, Warning, Error, Critical };
```

### 日志宏

| 宏 | 等级 | Release 行为 |
|-------|-------|------------------|
| `CLOG` | Critical | 可用 |
| `ELOG` | Error | 可用 |
| `WLOG` | Warning | 可用 |
| `ILOG` | Info | 可用 |
| `TLOG` | Trace | `NullLog`（空操作） |
| `DLOG` | Debug | `NullLog`（空操作） |

```cpp
ELOG << "连接失败: " << err;
WLOG << "将在 " << delay << "ms 后重试";
```

### 消息框宏

| 宏 | 等级 | Release 行为 |
|-------|-------|------------------|
| `CMSG` / `EMSG` / `WMSG` / `IMSG` | C/E/W/I | `NullLog`（空操作） |
| `DMSG` / `TMSG` | D/T | `NullLog`（空操作） |

`EMSG(log_macro)` 弹出消息框，同时将内容复制到 `log_macro`：

```cpp
EMSG(ELOG) << "致命错误";  // 弹框 + ELOG 流
```

### Sink 管理

```cpp
// 注册自定义 sink（线程安全）
stdpp::log::add_sink(std::make_unique<MyCustomSink>());

// 移除所有 sink
stdpp::log::clear_sinks();

// 准备文件日志（替换已有的 FileSink）
stdpp::log::prepare_file_logging("logs/");
```

### LogSink 接口

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
stdpp::log::ConsoleManager::open_console();   // 分配/关联控制台窗口
stdpp::log::ConsoleManager::clear_console();  // 清屏
stdpp::log::ConsoleManager::set_io_fast();    // 禁用 stdio 缓冲
```

## 自定义 Sink

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

// 注册
stdpp::log::add_sink(std::make_unique<JsonSink>("app.json"));
// 现在 WLOG、ELOG、CLOG 也会输出 JSON 格式
```

## 线程安全

- **Sink 注册**（`add_sink`、`clear_sinks`）— 由内部 mutex 保护。
- **Sink `write()` 调用** — 在共享锁下进行分发。每个 `FileSink` 有独立的 mutex 保护 ofstream 访问。
- **日志等级变更**（`set_level`）— 原子存储，无锁。
- **`LogMessage` 流** — 天然单线程（局部变量）。不要在线程间共享同一个 `LogMessage`。

## 测试

```cpp
// test_logger.cpp — 编译: cl /std:c++20 /EHsc test_logger.cpp
#include "logger.hpp"
#include <cassert>
#include <sstream>

void test_console_sink() {
    // ConsoleSink 写入 stdout（可通过目视或捕获验证）
    auto sink = std::make_unique<stdpp::log::ConsoleSink>();
    assert(sink->get_level() == stdpp::log::Level::Trace);
    sink->set_level(stdpp::log::Level::Error);
    assert(sink->get_level() == stdpp::log::Level::Error);
}

void test_file_sink() {
    auto sink = std::make_unique<stdpp::log::FileSink>("test_logs");
    assert(sink->get_level() == stdpp::log::Level::Trace);
    // 验证 test_logs/logs.log 已创建
}

void test_log_message() {
    stdpp::log::LogMessage msg(stdpp::log::Level::Info,
                               std::source_location::current());
    msg << "你好 " << 42;
    // msg 析构函数调用 log() — 写入已注册的 sink
}

void test_null_log() {
    stdpp::log::NullLog nl;
    nl << "这句 " << "被忽略";  // 空操作，无副作用
}

int main() {
    test_console_sink();
    test_file_sink();
    test_log_message();
    test_null_log();
}
```
