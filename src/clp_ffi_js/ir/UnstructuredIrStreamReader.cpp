#include "UnstructuredIrStreamReader.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <clp/Array.hpp>
#include <clp/ErrorCode.hpp>
#include <clp/ir/LogEventDeserializer.hpp>
#include <clp/ir/types.hpp>
#include <clp/TraceableException.hpp>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <spdlog/spdlog.h>

#include <clp_ffi_js/ClpFfiJsException.hpp>
#include <clp_ffi_js/constants.hpp>
#include <clp_ffi_js/ir/LogEventWithFilterData.hpp>
#include <clp_ffi_js/ir/StreamReader.hpp>
#include <clp_ffi_js/ir/StreamReaderDataContext.hpp>

namespace clp_ffi_js::ir {

using namespace std::literals::string_literals;
using clp::ir::four_byte_encoded_variable_t;

auto UnstructuredIrStreamReader::create(
        std::unique_ptr<ZstdDecompressor>&& zstd_decompressor,
        clp::Array<char> data_array
) -> UnstructuredIrStreamReader {
    auto result{UnstructuredIrDeserializer::create(*zstd_decompressor)};
    if (result.has_error()) {
        auto const error_code{result.error()};
        throw ClpFfiJsException{
                clp::ErrorCode::ErrorCode_Failure,
                __FILENAME__,
                __LINE__,
                std::format(
                        "Failed to create deserializer: {} {}",
                        error_code.category().name(),
                        error_code.message()
                )
        };
    }
    auto data_context = StreamReaderDataContext<UnstructuredIrDeserializer>(
            std::move(data_array),
            std::move(zstd_decompressor),
            std::move(result.value())
    );
    return UnstructuredIrStreamReader(std::move(data_context));
}

auto UnstructuredIrStreamReader::get_num_events_buffered() const -> size_t {
    return m_encoded_log_events.size();
}

auto UnstructuredIrStreamReader::get_filtered_log_event_map() const -> FilteredLogEventMapTsType {
    if (false == m_filtered_log_event_map.has_value()) {
        return FilteredLogEventMapTsType{emscripten::val::null()};
    }

    return FilteredLogEventMapTsType{emscripten::val::array(m_filtered_log_event_map.value())};
}

void UnstructuredIrStreamReader::filter_log_events(LogLevelFilterTsType const& log_level_filter) {
    generic_filter_log_events(m_filtered_log_event_map, log_level_filter, m_encoded_log_events);
}

auto UnstructuredIrStreamReader::deserialize_stream() -> size_t {
    if (nullptr == m_stream_reader_data_context) {
        return m_encoded_log_events.size();
    }

    constexpr size_t cDefaultNumReservedLogEvents{500'000};
    m_encoded_log_events.reserve(cDefaultNumReservedLogEvents);

    while (true) {
        auto result{m_stream_reader_data_context->get_deserializer().deserialize_log_event()};
        if (result.has_error()) {
            auto const error{result.error()};
            if (std::errc::no_message_available == error) {
                break;
            }
            if (std::errc::result_out_of_range == error) {
                SPDLOG_ERROR("File contains an incomplete IR stream");
                break;
            }
            throw ClpFfiJsException{
                    clp::ErrorCode::ErrorCode_Corrupt,
                    __FILENAME__,
                    __LINE__,
                    std::format(
                            "Failed to deserialize: {}:{}",
                            error.category().name(),
                            error.message()
                    )
            };
        }
        auto const& log_event = result.value();
        auto const& message = log_event.get_message();

        auto const& logtype = message.get_logtype();
        constexpr size_t cLogLevelPositionInMessages{1};
        LogLevel log_level{LogLevel::NONE};
        if (logtype.length() > cLogLevelPositionInMessages) {
            // NOLINTNEXTLINE(readability-qualified-auto)
            auto const log_level_name_it{std::find_if(
                    cLogLevelNames.begin() + static_cast<size_t>(cValidLogLevelsBeginIdx),
                    cLogLevelNames.end(),
                    [&](std::string_view level) {
                        return logtype.substr(cLogLevelPositionInMessages).starts_with(level);
                    }
            )};
            if (log_level_name_it != cLogLevelNames.end()) {
                log_level = static_cast<LogLevel>(
                        std::distance(cLogLevelNames.begin(), log_level_name_it)
                );
            }
        }

        m_encoded_log_events.emplace_back(log_event, log_level, log_event.get_timestamp());
    }
    m_stream_reader_data_context.reset(nullptr);
    return m_encoded_log_events.size();
}

auto UnstructuredIrStreamReader::decode_range(size_t begin_idx, size_t end_idx, bool use_filter)
        const -> DecodedResultsTsType {
    auto log_event_to_string = [this](UnstructuredLogEvent const& log_event) -> std::string {
        std::string message;
        auto const parsed{log_event.get_message().decode_and_unparse()};
        if (false == parsed.has_value()) {
            throw ClpFfiJsException{
                    clp::ErrorCode::ErrorCode_Failure,
                    __FILENAME__,
                    __LINE__,
                    "Failed to decode message"
            };
        }
        message = parsed.value();
        m_ts_pattern.insert_formatted_timestamp(log_event.get_timestamp(), message);
        return message;
    };

    return generic_decode_range(
            begin_idx,
            end_idx,
            m_filtered_log_event_map,
            m_encoded_log_events,
            log_event_to_string,
            use_filter
    );
}

auto UnstructuredIrStreamReader::get_log_event_idx_by_timestamp(
        clp::ir::epoch_time_ms_t const timestamp
) -> LogEventIdxTsType {
    return generic_get_log_event_idx_by_timestamp(m_encoded_log_events, timestamp);
}

UnstructuredIrStreamReader::UnstructuredIrStreamReader(
        StreamReaderDataContext<UnstructuredIrDeserializer>&& stream_reader_data_context
)
        : m_stream_reader_data_context{std::make_unique<
                  StreamReaderDataContext<UnstructuredIrDeserializer>>(
                  std::move(stream_reader_data_context)
          )},
          m_ts_pattern{m_stream_reader_data_context->get_deserializer().get_timestamp_pattern()} {}

}  // namespace clp_ffi_js::ir
