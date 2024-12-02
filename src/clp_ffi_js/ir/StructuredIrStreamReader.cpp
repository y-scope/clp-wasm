#include "StructuredIrStreamReader.hpp"

#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <clp/Array.hpp>
#include <clp/ErrorCode.hpp>
#include <clp/ffi/ir_stream/Deserializer.hpp>
#include <clp/ffi/KeyValuePairLogEvent.hpp>
#include <clp/TraceableException.hpp>
#include <emscripten/em_asm.h>
#include <emscripten/val.h>
#include <spdlog/spdlog.h>

#include <clp_ffi_js/ClpFfiJsException.hpp>
#include <clp_ffi_js/ir/LogEventWithFilterData.hpp>
#include <clp_ffi_js/ir/StreamReader.hpp>
#include <clp_ffi_js/ir/StreamReaderDataContext.hpp>
#include <clp_ffi_js/ir/StructuredIrUnitHandler.hpp>
#include <clp_ffi_js/ir/utils.hpp>

namespace clp_ffi_js::ir {
namespace {
constexpr std::string_view cEmptyJsonStr{"{}"};
constexpr std::string_view cLogLevelFilteringNotSupportedErrorMsg{
        "Log level filtering is not yet supported in this reader."
};
constexpr std::string_view cReaderOptionsLogLevelKey{"logLevelKey"};
constexpr std::string_view cReaderOptionsTimestampKey{"timestampKey"};
}  // namespace

auto StructuredIrStreamReader::create(
        std::unique_ptr<ZstdDecompressor>&& zstd_decompressor,
        clp::Array<char> data_array,
        ReaderOptions const& reader_options
) -> StructuredIrStreamReader {
    auto deserialized_log_events{
            std::make_shared<std::vector<LogEventWithFilterData<StructuredLogEvent>>>()
    };
    auto result{StructuredIrDeserializer::create(
            *zstd_decompressor,
            StructuredIrUnitHandler{
                    deserialized_log_events,
                    reader_options[cReaderOptionsLogLevelKey.data()].as<std::string>(),
                    reader_options[cReaderOptionsTimestampKey.data()].as<std::string>()
            }
    )};
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
    StreamReaderDataContext<StructuredIrDeserializer> data_context{
            std::move(data_array),
            std::move(zstd_decompressor),
            std::move(result.value())
    };
    return StructuredIrStreamReader{std::move(data_context), std::move(deserialized_log_events)};
}

auto StructuredIrStreamReader::get_num_events_buffered() const -> size_t {
    return m_deserialized_log_events->size();
}

auto StructuredIrStreamReader::get_filtered_log_event_map() const -> FilteredLogEventMapTsType {
    if (false == m_filtered_log_event_map.has_value()) {
        return FilteredLogEventMapTsType{emscripten::val::null()};
    }

    return FilteredLogEventMapTsType{emscripten::val::array(m_filtered_log_event_map.value())};
}

void StructuredIrStreamReader::filter_log_events(LogLevelFilterTsType const& log_level_filter) {
    filter_deserialized_events(
            m_filtered_log_event_map,
            log_level_filter,
            *m_deserialized_log_events
    );
}

auto StructuredIrStreamReader::deserialize_stream() -> size_t {
    if (nullptr == m_stream_reader_data_context) {
        return m_deserialized_log_events->size();
    }

    constexpr size_t cDefaultNumReservedLogEvents{500'000};
    m_deserialized_log_events->reserve(cDefaultNumReservedLogEvents);
    auto& reader{m_stream_reader_data_context->get_reader()};
    auto& deserializer = m_stream_reader_data_context->get_deserializer();

    while (false == deserializer.is_stream_completed()) {
        auto result{deserializer.deserialize_next_ir_unit(reader)};
        if (false == result.has_error()) {
            continue;
        }
        auto const error{result.error()};
        if (std::errc::result_out_of_range == error) {
            SPDLOG_ERROR("File contains an incomplete IR stream");
            break;
        }
        throw ClpFfiJsException{
                clp::ErrorCode::ErrorCode_Corrupt,
                __FILENAME__,
                __LINE__,
                std::format(
                        "Failed to deserialize IR unit: {}:{}",
                        error.category().name(),
                        error.message()
                )
        };
    }
    m_stream_reader_data_context.reset(nullptr);
    return m_deserialized_log_events->size();
}

auto StructuredIrStreamReader::decode_range(size_t begin_idx, size_t end_idx, bool use_filter) const
        -> DecodedResultsTsType {
    if (use_filter && false == m_filtered_log_event_map.has_value()) {
        return DecodedResultsTsType{emscripten::val::null()};
    }

    size_t length{0};
    if (use_filter) {
        length = m_filtered_log_event_map->size();
    } else {
        length = m_deserialized_log_events->size();
    }
    if (length < end_idx || begin_idx > end_idx) {
        return DecodedResultsTsType{emscripten::val::null()};
    }

    if (m_deserialized_log_events->size() < end_idx || begin_idx > end_idx) {
        return DecodedResultsTsType{emscripten::val::null()};
    }

    auto const results{emscripten::val::array()};

    for (size_t log_event_idx = begin_idx; log_event_idx < end_idx; ++log_event_idx) {
        auto const& log_event_with_filter_data{m_deserialized_log_events->at(log_event_idx)};
        auto const& structured_log_event = log_event_with_filter_data.get_log_event();

        auto const json_result{structured_log_event.serialize_to_json()};
        std::string json_str{cEmptyJsonStr};
        if (false == json_result.has_value()) {
            auto error_code{json_result.error()};
            SPDLOG_ERROR(
                    "Failed to deserialize log event to JSON: {}:{}",
                    error_code.category().name(),
                    error_code.message()
            );
        } else {
            json_str = json_result.value().dump();
        }

        EM_ASM(
                { Emval.toValue($0).push([UTF8ToString($1), $2, $3, $4]); },
                results.as_handle(),
                json_str.c_str(),
                log_event_with_filter_data.get_timestamp(),
                log_event_with_filter_data.get_log_level(),
                log_event_idx + 1
        );
    }

    return DecodedResultsTsType(results);
}

StructuredIrStreamReader::StructuredIrStreamReader(
        StreamReaderDataContext<StructuredIrDeserializer>&& stream_reader_data_context,
        std::shared_ptr<std::vector<LogEventWithFilterData<StructuredLogEvent>>>
                deserialized_log_events
)
        : m_deserialized_log_events{std::move(deserialized_log_events)},
          m_stream_reader_data_context{
                  std::make_unique<StreamReaderDataContext<StructuredIrDeserializer>>(
                          std::move(stream_reader_data_context)
                  )
          } {}
}  // namespace clp_ffi_js::ir
