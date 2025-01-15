#ifndef CLP_FFI_JS_IR_STREAMREADER_HPP
#define CLP_FFI_JS_IR_STREAMREADER_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <clp/ir/types.hpp>
#include <clp/streaming_compression/zstd/Decompressor.hpp>
#include <clp/type_utils.hpp>
#include <emscripten/em_asm.h>
#include <emscripten/val.h>
#include <spdlog/spdlog.h>

#include <clp_ffi_js/constants.hpp>
#include <clp_ffi_js/ir/LogEventWithFilterData.hpp>

namespace clp_ffi_js::ir {
// JS types used as inputs
EMSCRIPTEN_DECLARE_VAL_TYPE(DataArrayTsType);
EMSCRIPTEN_DECLARE_VAL_TYPE(LogLevelFilterTsType);
EMSCRIPTEN_DECLARE_VAL_TYPE(ReaderOptions);

// JS types used as outputs
EMSCRIPTEN_DECLARE_VAL_TYPE(DecodedResultsTsType);
EMSCRIPTEN_DECLARE_VAL_TYPE(FilteredLogEventMapTsType);
EMSCRIPTEN_DECLARE_VAL_TYPE(LogEventIdxTsType);

enum class StreamType : uint8_t {
    Structured,
    Unstructured,
};

template <typename LogEvent>
using LogEvents = std::vector<LogEventWithFilterData<LogEvent>>;

/**
 * Mapping between an index in the filtered log events collection to an index in the unfiltered
 * log events collection.
 */
using FilteredLogEventsMap = std::optional<std::vector<size_t>>;

/**
 * Class to deserialize and decode Zstandard-compressed CLP IR streams as well as format decoded
 * log events.
 */
class StreamReader {
public:
    using ZstdDecompressor = clp::streaming_compression::zstd::Decompressor;

    /**
     * Creates a `StreamReader` to read from the given array.
     *
     * @param data_array An array containing a Zstandard-compressed IR stream.
     * @param reader_options
     * @return The created instance.
     * @throw ClpFfiJsException if any error occurs.
     */
    [[nodiscard]] static auto create(
            DataArrayTsType const& data_array,
            ReaderOptions const& reader_options
    ) -> std::unique_ptr<StreamReader>;

    // Destructor
    virtual ~StreamReader() = default;

    // Disable copy constructor and assignment operator
    StreamReader(StreamReader const&) = delete;
    auto operator=(StreamReader const&) -> StreamReader& = delete;

    // Define default move constructor
    StreamReader(StreamReader&&) = default;
    // Delete move assignment operator since it's also disabled in `clp::ir::LogEventDeserializer`.
    auto operator=(StreamReader&&) -> StreamReader& = delete;

    // Methods
    [[nodiscard]] virtual auto get_ir_stream_type() const -> StreamType = 0;

    /**
     * @return The number of events buffered.
     */
    [[nodiscard]] virtual auto get_num_events_buffered() const -> size_t = 0;

    /**
     * @return The filtered log events map.
     */
    [[nodiscard]] virtual auto get_filtered_log_event_map() const -> FilteredLogEventMapTsType = 0;

    /**
     * Generates a filtered collection from all log events.
     *
     * @param log_level_filter Array of selected log levels
     */
    virtual void filter_log_events(LogLevelFilterTsType const& log_level_filter) = 0;

    /**
     * Deserializes all log events in the stream.
     *
     * @return The number of successfully deserialized ("valid") log events.
     * @throw ClpFfiJsException if an error occurs during deserialization.
     */
    [[nodiscard]] virtual auto deserialize_stream() -> size_t = 0;

    /**
     * Decodes log events in the range `[beginIdx, endIdx)` of the filtered or unfiltered
     * (depending on the value of `useFilter`) log events collection.
     *
     * @param begin_idx
     * @param end_idx
     * @param use_filter Whether to decode from the filtered or unfiltered log events collection.
     * @return An array where each element is a decoded log event represented by an array of:
     * - The log event's message
     * - The log event's timestamp as milliseconds since the Unix epoch
     * - The log event's log level as an integer that indexes into `cLogLevelNames`
     * - The log event's number (1-indexed) in the stream
     * @return null if any log event in the range doesn't exist (e.g. the range exceeds the number
     * of log events in the collection).
     * @throw ClpFfiJsException if a message cannot be decoded.
     */
    [[nodiscard]] virtual auto decode_range(size_t begin_idx, size_t end_idx, bool use_filter) const
            -> DecodedResultsTsType = 0;
    /**
     * Finds the index of the last log event that matches or next to the given timestamp.
     *
     * @param timestamp The timestamp to search for, in milliseconds since the Unix epoch.
     * @return The last index of the log event whose timestamp is smaller than or equal to the
     * `timestamp`.
     * @return `0` if all log event timestamps are larger than the target.
     * @return null if no log event exists in the stream.
     */
    [[nodiscard]] virtual auto get_log_event_idx_by_timestamp(clp::ir::epoch_time_ms_t timestamp
    ) -> LogEventIdxTsType = 0;

protected:
    explicit StreamReader() = default;

    /**
     * Templated implementation of `decode_range` that uses `log_event_to_string` to convert
     * `log_event` to a string for the returned result.
     *
     * @tparam LogEvent
     * @tparam ToStringFunc Function to convert a log event into a string.
     * @param begin_idx
     * @param end_idx
     * @param filtered_log_event_map
     * @param log_events
     * @param use_filter
     * @param log_event_to_string
     * @return See `decode_range`.
     * @throws Propagates `ToStringFunc`'s exceptions.
     */
    template <typename LogEvent, typename ToStringFunc>
    requires requires(ToStringFunc func, LogEvent const& log_event) {
        {
            func(log_event)
        } -> std::convertible_to<std::string>;
    }
    static auto generic_decode_range(
            size_t begin_idx,
            size_t end_idx,
            FilteredLogEventsMap const& filtered_log_event_map,
            LogEvents<LogEvent> const& log_events,
            ToStringFunc log_event_to_string,
            bool use_filter
    ) -> DecodedResultsTsType;

    /**
     * Templated implementation of `filter_log_events`.
     *
     * @tparam LogEvent
     * @param log_level_filter
     * @param log_events Derived class's log events.
     * @param log_events
     * @param[out] filtered_log_event_map Returns the filtered log events.
     */
    template <typename LogEvent>
    static auto generic_filter_log_events(
            FilteredLogEventsMap& filtered_log_event_map,
            LogLevelFilterTsType const& log_level_filter,
            LogEvents<LogEvent> const& log_events
    ) -> void;

    /**
     * Templated implementation of `get_log_event_idx_by_timestamp`.
     *
     * @tparam LogEvent
     * @param log_events
     * @param timestamp
     * @return the best matched log event index.
     */
    template <typename LogEvent>
    auto generic_get_log_event_idx_by_timestamp(
            LogEvents<LogEvent> const& log_events,
            clp::ir::epoch_time_ms_t timestamp
    ) -> LogEventIdxTsType;
};

template <typename LogEvent, typename ToStringFunc>
requires requires(ToStringFunc func, LogEvent const& log_event) {
    {
        func(log_event)
    } -> std::convertible_to<std::string>;
}
auto StreamReader::generic_decode_range(
        size_t begin_idx,
        size_t end_idx,
        FilteredLogEventsMap const& filtered_log_event_map,
        LogEvents<LogEvent> const& log_events,
        ToStringFunc log_event_to_string,
        bool use_filter
) -> DecodedResultsTsType {
    if (use_filter && false == filtered_log_event_map.has_value()) {
        return DecodedResultsTsType{emscripten::val::null()};
    }

    size_t length{0};
    if (use_filter) {
        length = filtered_log_event_map->size();
    } else {
        length = log_events.size();
    }
    if (length < end_idx || begin_idx > end_idx) {
        SPDLOG_ERROR("Invalid log event index range: {}-{}", begin_idx, end_idx);
        return DecodedResultsTsType{emscripten::val::null()};
    }

    auto const results{emscripten::val::array()};

    for (size_t i = begin_idx; i < end_idx; ++i) {
        size_t log_event_idx{0};
        if (use_filter) {
            log_event_idx = filtered_log_event_map->at(i);
        } else {
            log_event_idx = i;
        }

        auto const& log_event_with_filter_data{log_events.at(log_event_idx)};
        auto const& log_event = log_event_with_filter_data.get_log_event();
        auto const& timestamp = log_event_with_filter_data.get_timestamp();
        auto const& log_level = log_event_with_filter_data.get_log_level();

        EM_ASM(
                { Emval.toValue($0).push([UTF8ToString($1), $2, $3, $4]); },
                results.as_handle(),
                log_event_to_string(log_event).c_str(),
                timestamp,
                log_level,
                log_event_idx + 1
        );
    }

    return DecodedResultsTsType(results);
}

template <typename LogEvent>
auto StreamReader::generic_filter_log_events(
        FilteredLogEventsMap& filtered_log_event_map,
        LogLevelFilterTsType const& log_level_filter,
        LogEvents<LogEvent> const& log_events
) -> void {
    if (log_level_filter.isNull()) {
        filtered_log_event_map.reset();
        return;
    }

    filtered_log_event_map.emplace();
    auto filter_levels
            = emscripten::vecFromJSArray<std::underlying_type_t<LogLevel>>(log_level_filter);

    for (size_t log_event_idx = 0; log_event_idx < log_events.size(); ++log_event_idx) {
        auto const& log_event = log_events[log_event_idx];
        if (std::ranges::find(
                    filter_levels,
                    clp::enum_to_underlying_type(log_event.get_log_level())
            )
            != filter_levels.end())
        {
            filtered_log_event_map->emplace_back(log_event_idx);
        }
    }
}

template <typename LogEvent>
auto StreamReader::generic_get_log_event_idx_by_timestamp(
        LogEvents<LogEvent> const& log_events,
        clp::ir::epoch_time_ms_t timestamp
) -> LogEventIdxTsType {
    if (log_events.empty()) {
        return LogEventIdxTsType{emscripten::val::null()};
    }

    auto first_greater_it{std::upper_bound(
            log_events.begin(),
            log_events.end(),
            timestamp,
            [](clp::ir::epoch_time_ms_t ts, LogEventWithFilterData<LogEvent> const& log_event) {
                return ts < log_event.get_timestamp();
            }
    )};

    if (first_greater_it == log_events.begin()) {
        return LogEventIdxTsType{emscripten::val(0)};
    }

    auto const first_greater_idx{std::distance(log_events.begin(), first_greater_it)};

    return LogEventIdxTsType{emscripten::val(first_greater_idx - 1)};
}
}  // namespace clp_ffi_js::ir

#endif  // CLP_FFI_JS_IR_STREAMREADER_HPP
