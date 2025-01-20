#include "StructuredIrUnitHandler.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_utils.hpp>
#include <utility>

#include <clp/ffi/ir_stream/decoding_methods.hpp>
#include <clp/ffi/KeyValuePairLogEvent.hpp>
#include <clp/ffi/SchemaTree.hpp>
#include <clp/ffi/Value.hpp>
#include <clp/ir/types.hpp>
#include <clp/time_types.hpp>
#include <emscripten/val.h>
#include <spdlog/spdlog.h>

#include <clp_ffi_js/constants.hpp>
#include <clp_ffi_js/ir/LogEventWithFilterData.hpp>

namespace clp_ffi_js::ir {
namespace {
/**
 * Parses a string to determine the corresponding `LogLevel` enum value.
 * @param str
 * @return `LogLevel` enum corresponding to `str` if `str` matches a string in `cLogLevelNames`.
 * @return `LogLevel::LogLevelNone` otherwise.
 */
auto parse_log_level(std::string_view str) -> LogLevel;

auto parse_log_level(std::string_view str) -> LogLevel {
    // Convert the string to uppercase.
    std::string log_level_name_upper_case{str};
    std::ranges::transform(
            log_level_name_upper_case.begin(),
            log_level_name_upper_case.end(),
            log_level_name_upper_case.begin(),
            [](unsigned char c) { return std::toupper(c); }
    );

    auto const* it = std::ranges::find(
            cLogLevelNames.begin() + clp::enum_to_underlying_type(cValidLogLevelsBeginIdx),
            cLogLevelNames.end(),
            log_level_name_upper_case
    );
    if (it == cLogLevelNames.end()) {
        return LogLevel::LogLevelNone;
    }

    return static_cast<LogLevel>(std::distance(cLogLevelNames.begin(), it));
}
}  // namespace

auto StructuredIrUnitHandler::handle_log_event(StructuredLogEvent&& log_event
) -> clp::ffi::ir_stream::IRErrorCode {
    auto const& id_value_pairs{log_event.get_node_id_value_pairs()};
    auto const timestamp = get_timestamp(id_value_pairs);
    auto const log_level = get_log_level(id_value_pairs);

    m_deserialized_log_events->emplace_back(std::move(log_event), log_level, timestamp);

    return clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
}

auto StructuredIrUnitHandler::handle_utc_offset_change(
        [[maybe_unused]] clp::UtcOffset utc_offset_old,
        [[maybe_unused]] clp::UtcOffset utc_offset_new
) -> clp::ffi::ir_stream::IRErrorCode {
    SPDLOG_WARN("UTC offset change packets aren't handled currently.");
    return clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
}

auto StructuredIrUnitHandler::handle_schema_tree_node_insertion(
        clp::ffi::SchemaTree::NodeLocator schema_tree_node_locator
) -> clp::ffi::ir_stream::IRErrorCode {
    ++m_current_node_id;

    auto const& key_name{schema_tree_node_locator.get_key_name()};
    if (key_name == m_log_level_key) {
        m_log_level_node_id.emplace(m_current_node_id);
    } else if (key_name == m_timestamp_key) {
        m_timestamp_node_id.emplace(m_current_node_id);
    }

    return clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
}

auto StructuredIrUnitHandler::handle_end_of_stream() -> clp::ffi::ir_stream::IRErrorCode {
    return clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
}

auto StructuredIrUnitHandler::get_log_level(
        StructuredLogEvent::NodeIdValuePairs const& id_value_pairs
) const -> LogLevel {
    LogLevel log_level{LogLevel::LogLevelNone};

    if (false == m_log_level_node_id.has_value()) {
        return log_level;
    }
    auto const& optional_log_level_value{id_value_pairs.at(m_log_level_node_id.value())};
    if (false == optional_log_level_value.has_value()) {
        return log_level;
    }
    auto const log_level_value = optional_log_level_value.value();

    if (log_level_value.is<std::string>()) {
        auto const& log_level_str = log_level_value.get_immutable_view<std::string>();
        log_level = parse_log_level(log_level_str);
    } else if (log_level_value.is<clp::ffi::value_int_t>()) {
        auto const& log_level_int = log_level_value.get_immutable_view<clp::ffi::value_int_t>();
        if (log_level_int >= clp::enum_to_underlying_type(cValidLogLevelsBeginIdx)
            && log_level_int < clp::enum_to_underlying_type(LogLevel::LogLevelLength))
        {
            log_level = static_cast<LogLevel>(log_level_int);
        }
    } else {
        auto log_event_idx = m_deserialized_log_events->size();
        SPDLOG_ERROR(
                "Authoritative log level's value is not an int or string for log event index {}",
                log_event_idx
        );
    }

    return log_level;
}

auto StructuredIrUnitHandler::get_timestamp(
        StructuredLogEvent::NodeIdValuePairs const& id_value_pairs
) const -> clp::ir::epoch_time_ms_t {
    clp::ir::epoch_time_ms_t timestamp{0};

    if (false == m_timestamp_node_id.has_value()) {
        return timestamp;
    }
    auto const& optional_timestamp_value{id_value_pairs.at(m_timestamp_node_id.value())};
    if (false == optional_timestamp_value.has_value()) {
        return timestamp;
    }
    auto const timestamp_value = optional_timestamp_value.value();

    if (timestamp_value.is<clp::ffi::value_int_t>()) {
        timestamp = static_cast<clp::ir::epoch_time_ms_t>(
                timestamp_value.get_immutable_view<clp::ffi::value_int_t>()
        );
    } else {
        // TODO: Add support for parsing string-type timestamp values.
        auto log_event_idx = m_deserialized_log_events->size();
        SPDLOG_ERROR(
                "Authoritative timestamp's value is not an int for log event index {}",
                log_event_idx
        );
    }

    return timestamp;
}
}  // namespace clp_ffi_js::ir
