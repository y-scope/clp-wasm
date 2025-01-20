#ifndef CLP_FFI_JS_CONSTANTS_HPP
#define CLP_FFI_JS_CONSTANTS_HPP

#include <array>
#include <cstdint>
#include <string_view>
#include <type_utils.hpp>

namespace clp_ffi_js {
/**
 * Enum of known log levels.
 */
enum class LogLevel : std::uint8_t {
    LogLevelNone = 0,  // This isn't a valid log level.
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    LogLevelLength,
};
constexpr LogLevel cValidLogLevelsBeginIdx{LogLevel::Trace};

/**
 * Strings corresponding to `LogLevel`.
 *
 * NOTE: These must be kept in sync manually.
 */
constexpr std::array<std::string_view, clp::enum_to_underlying_type(LogLevel::LogLevelLength)>
        cLogLevelNames{
                "NONE",  // This isn't a valid log level.
                "TRACE",
                "DEBUG",
                "INFO",
                "WARN",
                "ERROR",
                "FATAL",
        };
}  // namespace clp_ffi_js

#endif  // CLP_FFI_JS_CONSTANTS_HPP
