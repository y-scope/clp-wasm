#include "ClpIrV1Decoder.hpp"

#include <emscripten/em_asm.h>
#include <emscripten/val.h>

#include <clp/ErrorCode.hpp>
#include <clp/ffi/ir_stream/decoding_methods.hpp>
#include <clp/ir/LogEvent.hpp>
#include <clp/ir/LogEventDeserializer.hpp>
#include <clp/ir/types.hpp>
#include <clp/streaming_compression/zstd/Decompressor.hpp>
#include <clp/TimestampPattern.hpp>
#include <clp/TraceableException.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <system_error>
#include <utility>

#include <spdlog/spdlog.h>

#include "ClpJsException.hpp"
#include "constants.hpp"

using namespace std::literals::string_literals;

// Constants
namespace {
constexpr size_t cDefaultNumCharsPerMessage{512};
constexpr size_t cDefaultNumLogEvents{500'000};
constexpr size_t cFullRangeEndIdx{0};
constexpr size_t cLogLevelNone{0};
}  // namespace

auto ClpIrV1Decoder::create(emscripten::val const& data_array) -> std::unique_ptr<ClpIrV1Decoder> {
    auto length{data_array["length"].as<size_t>()};
    SPDLOG_INFO("ClpIrV1Decoder::ClpIrV1Decoder() got buffer of length={}", length);

    auto data_buffer{std::make_unique<char const[]>(length)};
    emscripten::val::module_property("HEAPU8")
            .call<void>("set", data_array, reinterpret_cast<uintptr_t>(data_buffer.get()));

    auto zstd_decompressor{std::make_shared<clp::streaming_compression::zstd::Decompressor>()};
    zstd_decompressor->open(data_buffer.get(), length);

    bool is_four_bytes_encoding{true};
    if (auto const err{
                clp::ffi::ir_stream::get_encoding_type(*zstd_decompressor, is_four_bytes_encoding)
        };
        clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success != err)
    {
        SPDLOG_CRITICAL("Failed to decode encoding type, err={}", err);
        throw ClpJsException(
                clp::ErrorCode::ErrorCode_MetadataCorrupted,
                __FILENAME__,
                __LINE__,
                "Failed to decode encoding type.."
        );
    }

    if (false == is_four_bytes_encoding) {
        throw ClpJsException(
                clp::ErrorCode::ErrorCode_Unsupported,
                __FILENAME__,
                __LINE__,
                "Is not four byte encoding."
        );
    }
    auto result{clp::ir::LogEventDeserializer<clp::ir::four_byte_encoded_variable_t>::create(
            *zstd_decompressor
    )};
    if (result.has_error()) {
        auto const error_code{result.error()};
        SPDLOG_CRITICAL(
                "Failed to decompress: {}:{}",
                error_code.category().name(),
                error_code.message()
        );
        throw ClpJsException(
                clp::ErrorCode::ErrorCode_MetadataCorrupted,
                __FILENAME__,
                __LINE__,
                "Failed to decompress"
        );
    }

    std::unique_ptr<ClpIrV1Decoder> ptr(
            new ClpIrV1Decoder(std::move(data_buffer), zstd_decompressor, std::move(result.value()))
    );
    return ptr;
}

auto ClpIrV1Decoder::get_estimated_num_events() -> size_t const {
    return m_log_events.size();
}

auto ClpIrV1Decoder::build_idx(size_t begin_idx, size_t end_idx) -> emscripten::val const {
    if (0 != begin_idx && cFullRangeEndIdx != end_idx) {
        throw ClpJsException(
                clp::ErrorCode::ErrorCode_Unsupported,
                __FILENAME__,
                __LINE__,
                "Partial range indexing building is not yet supported."
        );
    }
    if (0 == m_log_events.capacity()) {
        m_log_events.reserve(cDefaultNumLogEvents);
        while (true) {
            auto const result{m_deserializer.deserialize_log_event()};
            if (false == result.has_error()) {
                m_log_events.emplace_back(result.value());
                continue;
            }
            auto const error{result.error()};
            if (std::errc::no_message_available == error) {
                break;
            }
            if (std::errc::result_out_of_range == error) {
                SPDLOG_ERROR("File contains an incomplete IR stream");
                break;
            }
            throw ClpJsException(
                    clp::ErrorCode::ErrorCode_Corrupt,
                    __FILENAME__,
                    __LINE__,
                    "Failed to decompress: "s + error.category().name() + ":" + error.message()
            );
        }
        m_data_buffer.reset(nullptr);
    }

    emscripten::val results{emscripten::val::object()};
    results.set("numValidEvents", m_log_events.size());
    results.set("numInvalidEvents", 0);
    return results;
}

auto ClpIrV1Decoder::decode(size_t begin_idx, size_t end_idx) -> emscripten::val const {
    if (0 > begin_idx || m_log_events.size() < end_idx || begin_idx >= end_idx) {
        return emscripten::val::null();
    }

    std::string message;
    message.reserve(cDefaultNumCharsPerMessage);
    emscripten::val results{emscripten::val::array()};
    std::span<clp::ir::LogEvent<clp::ir::four_byte_encoded_variable_t> const> log_events_span(
            m_log_events.begin() + begin_idx,
            m_log_events.begin() + end_idx
    );
    for (auto const& log_event : log_events_span) {
        message.clear();

        // TODO: Replace below handlers code by an OSS decoding method once it's added in the future
        auto constant_handler = [&](std::string const& value, size_t begin_pos, size_t length) {
            message.append(value, begin_pos, length);
        };

        auto encoded_int_handler = [&](clp::ir::four_byte_encoded_variable_t value) {
            message.append(clp::ffi::decode_integer_var(value));
        };

        auto encoded_float_handler = [&](clp::ir::four_byte_encoded_variable_t encoded_float) {
            message.append(clp::ffi::decode_float_var(encoded_float));
        };

        auto dict_var_handler = [&](std::string const& dict_var) { message.append(dict_var); };

        try {
            clp::ffi::ir_stream::generic_decode_message<true>(
                    log_event.get_logtype(),
                    log_event.get_encoded_vars(),
                    log_event.get_dict_vars(),
                    constant_handler,
                    encoded_int_handler,
                    encoded_float_handler,
                    dict_var_handler
            );
        } catch (clp::ffi::ir_stream::DecodingException const& e) {
            SPDLOG_ERROR("Failed to decode. Error Code: {}", e.get_error_code());
            break;
        }

        size_t log_level{cLogLevelNone};
        for (size_t i = 1; i < cLogLevelNames.size(); ++i) {
            if (message.substr(1).starts_with(cLogLevelNames[i])) {
                log_level = i;
                break;
            }
        }

        m_ts_pattern.insert_formatted_timestamp(log_event.get_timestamp(), message);

        EM_ASM(
                { Emval.toValue($0).push([UTF8ToString($1), $2, $3, $4]); },
                results.as_handle(),
                message.c_str(),
                log_event.get_timestamp(),
                log_level,
                begin_idx + (&log_event - log_events_span.data()) + 1
        );
    }

    return results;
}

ClpIrV1Decoder::ClpIrV1Decoder(
        std::unique_ptr<char const[]> data_buffer,
        std::shared_ptr<clp::streaming_compression::zstd::Decompressor> zstd_decompressor,
        clp::ir::LogEventDeserializer<clp::ir::four_byte_encoded_variable_t> deserializer
)
        : m_data_buffer{std::move(data_buffer)},
          m_zstd_decompressor{std::move(zstd_decompressor)},
          m_deserializer{std::move(deserializer)},
          m_ts_pattern{m_deserializer.get_timestamp_pattern()} {}
