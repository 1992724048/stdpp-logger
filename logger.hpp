// 2026-07-28 03:01:53

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <print>
#include <source_location>
#include <sstream>
#include <string_view>
#include <vector>
#include <windows.h>

namespace stdpp::log {
    enum class Level : std::uint8_t {
        None,
        Trace,
        Debug,
        Info,
        Warning,
        Error,
        Critical,
    };

    enum class LoggerType : std::uint8_t { Any, ConsoleLogger, FileLogger };

    namespace {
        inline constexpr auto kColorReset = "\033[0m";
        inline constexpr auto kColorRed = "\033[31m";
        inline constexpr auto kColorGreen = "\033[32m";
        inline constexpr auto kColorCyan = "\033[36m";
        inline constexpr auto kColorWhite = "\033[37m";
        inline constexpr auto kColorBrightRed = "\033[91m";
        inline constexpr auto kColorBrightYellow = "\033[93m";
        inline constexpr auto kColorBrightCyan = "\033[96m";
        inline constexpr auto kColorBrightMagenta = "\033[95m";

        constexpr auto get_level_text(const Level level) -> std::string_view {
            switch (level) {
                case Level::None:
                    return "NONE";
                case Level::Trace:
                    return "TRACE";
                case Level::Debug:
                    return "DEBUG";
                case Level::Info:
                    return "INFO";
                case Level::Warning:
                    return "WARNING";
                case Level::Error:
                    return "ERROR";
                case Level::Critical:
                    return "CRITICAL";
                default:
                    return "NONE";
            }
        }

        constexpr auto get_color_code(const Level level) -> std::string_view {
            switch (level) {
                case Level::None:
                    return kColorWhite;
                case Level::Trace:
                    return kColorBrightCyan;
                case Level::Debug:
                    return kColorCyan;
                case Level::Info:
                    return kColorGreen;
                case Level::Warning:
                    return kColorBrightYellow;
                case Level::Error:
                    return kColorRed;
                case Level::Critical:
                    return kColorBrightRed;
                default:
                    return kColorWhite;
            }
        }
    }

    class LogSink {
    public:
        virtual ~LogSink() = default;
        virtual auto write(Level level, std::string_view message) -> void = 0;

        auto set_level(const Level lvl) -> void {
            level_.store(lvl, std::memory_order_release);
        }

        [[nodiscard]] auto get_level() const -> Level {
            return level_.load(std::memory_order_acquire);
        }
    protected:
        std::atomic<Level> level_{Level::Trace};
    };

    class ConsoleSink final : public LogSink {
    public:
        auto write(const Level level, std::string_view message) -> void override {
            static const auto k_start_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - k_start_time).count();
            auto console_msg = std::format("◆ [{}{}{}] {:.3f}s {}{}{}\n", get_color_code(level), get_level_text(level), kColorReset, elapsed, kColorReset, message, kColorReset);
            std::print("{}", console_msg);
        }
    };

    class FileSink final : public LogSink {
    public:
        explicit FileSink(const std::filesystem::path& dir) {
            if (!is_directory(dir)) {
                create_directories(dir);
            }
            const auto log_file_path = std::format("{}/logs.log", dir.string());
            file_stream_ = std::ofstream(log_file_path, std::ios::out | std::ios::trunc | std::ios::binary);
            start_time_ = std::chrono::steady_clock::now();
        }

        auto write(const Level level, std::string_view message) -> void override {
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
            const auto file_msg = std::format("[{:.3f}s] [{}] {}\n", elapsed, get_level_text(level), message);
            std::scoped_lock lock(mutex_);
            if (file_stream_.is_open()) {
                file_stream_ << file_msg;
            }
        }

        auto flush() -> void {
            std::scoped_lock lock(mutex_);
            if (file_stream_.is_open()) {
                file_stream_ << std::flush;
            }
        }
    private:
        std::chrono::steady_clock::time_point start_time_;
        std::ofstream file_stream_;
        std::mutex mutex_;
    };

    struct LoggerState {
        std::vector<std::unique_ptr<LogSink>> sinks_;
        std::mutex mutex_;
    };

    inline auto logger_state() -> LoggerState& {
        static LoggerState state = [] -> LoggerState {
            LoggerState s;
            s.sinks_.push_back(std::make_unique<ConsoleSink>());
            return s;
        }();
        return state;
    }

    struct ConsoleManager {
        static auto open_console() -> void {
            // Must be called before any C++ stream I/O to avoid UB.
            std::ios::sync_with_stdio(false);

            FreeConsole();
            if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
                AllocConsole();
                ShowWindow(GetConsoleWindow(), SW_SHOW);
                freopen_s(nullptr, "CONOUT$", "w", stdout);
                freopen_s(nullptr, "CONOUT$", "w", stderr);
                freopen_s(nullptr, "CONIN$", "r", stdin);
            }

            enable_virtual_terminal();
            SetConsoleOutputCP(65001);
            SetConsoleCP(65001);
            clear_console();
            SetConsoleTitleA("Log");
            set_io_fast();
        }

        static auto set_io_fast() -> void {
            // Unbuffered stdout/stderr so std::print output is immediate.
            // (std::print writes to the C FILE* stdout, not std::cout.)
            setvbuf(stdout, nullptr, _IONBF, 0);
            setvbuf(stderr, nullptr, _IONBF, 0);
        }

        static auto enable_virtual_terminal() -> void {
            const HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h_out == INVALID_HANDLE_VALUE) {
                return;
            }
            DWORD dw_mode = 0;
            if (!GetConsoleMode(h_out, &dw_mode)) {
                return;
            }
            dw_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            dw_mode |= DISABLE_NEWLINE_AUTO_RETURN;
            SetConsoleMode(h_out, dw_mode);
        }

        static auto clear_console() -> void {
            const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (!GetConsoleScreenBufferInfo(console, &csbi)) {
                return;
            }

            const SMALL_RECT scroll_rect{.Left = 0, .Top = 0, .Right = csbi.dwSize.X, .Bottom = csbi.dwSize.Y};
            const COORD scroll_target{.X = 0, .Y = static_cast<SHORT>(0 - csbi.dwSize.Y)};
            CHAR_INFO fill;
            fill.Char.UnicodeChar = L' ';
            fill.Attributes = csbi.wAttributes;

            ScrollConsoleScreenBuffer(console, &scroll_rect, nullptr, scroll_target, &fill);

            csbi.dwCursorPosition = {.X = 0, .Y = 0};
            SetConsoleCursorPosition(console, csbi.dwCursorPosition);
        }
    };

    inline auto add_sink(std::unique_ptr<LogSink> sink) -> void {
        auto& [sinks_, mutex_] = logger_state();
        std::scoped_lock lock(mutex_);
        sinks_.push_back(std::move(sink));
    }

    inline auto clear_sinks() -> void {
        auto& [sinks_, mutex_] = logger_state();
        std::scoped_lock lock(mutex_);
        sinks_.clear();
    }

    inline auto set_level(const Level level, const LoggerType type = LoggerType::Any) -> void {
        auto& [sinks_, mutex_] = logger_state();
        std::scoped_lock lock(mutex_);
        for (auto& sink : sinks_) {
            if (type == LoggerType::Any) {
                sink->set_level(level);
            } else if (type == LoggerType::ConsoleLogger && dynamic_cast<ConsoleSink*>(sink.get())) {
                sink->set_level(level);
            } else if (type == LoggerType::FileLogger && dynamic_cast<FileSink*>(sink.get())) {
                sink->set_level(level);
            }
        }
    }

    [[nodiscard]] inline auto get_level(const LoggerType type) -> Level {
        auto& [sinks_, mutex_] = logger_state();
        std::scoped_lock lock(mutex_);
        if (type == LoggerType::Any) {
            auto min_level = Level::None;
            bool found = false;
            for (const auto& sink : sinks_) {
                if (const auto lvl = sink->get_level(); !found || lvl < min_level) {
                    min_level = lvl;
                    found = true;
                }
            }
            return min_level;
        }
        for (auto& sink : sinks_) {
            if (type == LoggerType::ConsoleLogger && dynamic_cast<ConsoleSink*>(sink.get())) {
                return sink->get_level();
            }
            if (type == LoggerType::FileLogger && dynamic_cast<FileSink*>(sink.get())) {
                return sink->get_level();
            }
        }
        return Level::None;
    }

    inline auto prepare_file_logging(const std::filesystem::path& dir) -> void {
        auto& [sinks_, mutex_] = logger_state();
        std::scoped_lock lock(mutex_);
        std::erase_if(sinks_,
                      [](const auto& s) -> auto {
                          return dynamic_cast<FileSink*>(s.get()) != nullptr;
                      });
        sinks_.push_back(std::make_unique<FileSink>(dir));
    }

    inline auto log(const Level level, const std::string_view file, int line, const std::string& msg, std::string_view /*func_name*/) -> void {
        auto& [sinks_, mutex_] = logger_state();
        std::string file_name = std::filesystem::path(file).filename().string();
        const std::string formatted = std::format("[{}:{}] {}", file_name, line, msg);

        std::scoped_lock lock(mutex_);
        for (const auto& sink : sinks_) {
            if (sink->get_level() != Level::None && sink->get_level() >= level) {
                sink->write(level, formatted);
            }
        }
    }

    class LogMessage {
    public:
        LogMessage(const Level level, const std::source_location& source) :
            level_(level),
            source_(source) {}

        ~LogMessage() {
            commit();
        }

        template<typename T>
        auto operator<<(const T& value) -> LogMessage& {
            stream_ << value;
            return *this;
        }

        auto operator<<(std::ostream& (*manip)(std::ostream&)) -> LogMessage& {
            stream_ << manip;
            return *this;
        }
    private:
        auto commit() const -> void {
            log(level_, source_.file_name(), source_.line(), stream_.str(), source_.function_name());
        }

        Level level_;
        std::source_location source_;
        std::ostringstream stream_;

        friend class MessageBoxHelper;
    };

    struct NullLog {
        template<class T>
        constexpr auto operator<<(const T&) noexcept -> NullLog& {
            return *this;
        }

        constexpr auto operator<<(std::ostream& (*)(std::ostream&)) noexcept -> NullLog& {
            return *this;
        }
    };

    class MessageBoxHelper {
    public:
        MessageBoxHelper(const Level level, const std::source_location& source) :
            level_(level),
            source_(source) {}

        template<typename T>
        auto operator<<(const T& value) -> MessageBoxHelper& {
            stream_ << value;
            return *this;
        }

        auto operator<<(std::ostream& (*manip)(std::ostream&)) -> MessageBoxHelper& {
            stream_ << manip;
            return *this;
        }

        auto operator()(LogMessage& log_msg) -> MessageBoxHelper& {
            log_msg.stream_ << stream_.str();
            log_msg.level_ = level_;
            log_msg.source_ = source_;
            return *this;
        }

        ~MessageBoxHelper() {
            show();
        }
    private:
        auto show() const -> void {
            UINT u_type = MB_OK | MB_TOPMOST | MB_SETFOREGROUND;
            std::string title;

            switch (level_) {
                case Level::Critical:
                    title = "严重错误 (Critical)";
                    u_type |= MB_ICONERROR;
                    break;
                case Level::Error:
                    title = "错误 (Error)";
                    u_type |= MB_ICONERROR;
                    break;
                case Level::Warning:
                    title = "警告 (Warning)";
                    u_type |= MB_ICONWARNING;
                    break;
                case Level::Info:
                    title = "信息 (Info)";
                    u_type |= MB_ICONINFORMATION;
                    break;
                case Level::Debug:
                    title = "调试 (Debug)";
                    u_type |= MB_ICONINFORMATION;
                    break;
                case Level::Trace:
                    title = "跟踪 (Trace)";
                    u_type |= MB_ICONINFORMATION;
                    break;
                default:
                    title = "未知 (Unknown)";
                    break;
            }

            std::string file_name = std::filesystem::path(source_.file_name()).filename().string();
            const std::string function_name = source_.function_name();
            const std::string separator(std::string("函数: ").size() + function_name.size(), '-');
            const std::string body = std::format("文件: {}:{}\n函数: {}\n{}\n消息: {}", file_name, source_.line(), function_name, separator, stream_.str());

            MessageBoxA(nullptr, body.c_str(), title.c_str(), u_type);
        }

        Level level_;
        std::source_location source_;
        std::ostringstream stream_;
    };
}

#define CLOG ::stdpp::log::LogMessage(::stdpp::log::Level::Critical, std::source_location::current())
#define ELOG ::stdpp::log::LogMessage(::stdpp::log::Level::Error,    std::source_location::current())
#define WLOG ::stdpp::log::LogMessage(::stdpp::log::Level::Warning,  std::source_location::current())
#define ILOG ::stdpp::log::LogMessage(::stdpp::log::Level::Info,     std::source_location::current())

#ifdef _DEBUG
#define TLOG ::stdpp::log::LogMessage(::stdpp::log::Level::Trace, std::source_location::current())
#define DLOG ::stdpp::log::LogMessage(::stdpp::log::Level::Debug, std::source_location::current())
#define DMSG ::stdpp::log::MessageBoxHelper(::stdpp::log::Level::Debug, std::source_location::current())
#define TMSG ::stdpp::log::MessageBoxHelper(::stdpp::log::Level::Trace, std::source_location::current())
#else
#define TLOG ::stdpp::log::NullLog()
#define DLOG ::stdpp::log::NullLog()
#define DMSG ::stdpp::log::NullLog()
#define TMSG ::stdpp::log::NullLog()
#endif

#ifndef NDEBUG
#define CMSG ::stdpp::log::MessageBoxHelper(::stdpp::log::Level::Critical, std::source_location::current())
#define EMSG ::stdpp::log::MessageBoxHelper(::stdpp::log::Level::Error,    std::source_location::current())
#define WMSG ::stdpp::log::MessageBoxHelper(::stdpp::log::Level::Warning,  std::source_location::current())
#define IMSG ::stdpp::log::MessageBoxHelper(::stdpp::log::Level::Info,     std::source_location::current())
#else
#define CMSG ::stdpp::log::NullLog()
#define EMSG ::stdpp::log::NullLog()
#define WMSG ::stdpp::log::NullLog()
#define IMSG ::stdpp::log::NullLog()
#endif

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
