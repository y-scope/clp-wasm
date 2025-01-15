#ifndef CLP_FFI_JS_IR_UNSTRUCTUREDIRSTREAMREADER_HPP
#define CLP_FFI_JS_IR_UNSTRUCTUREDIRSTREAMREADER_HPP

#include <Array.hpp>
#include <cstddef>
#include <memory>

#include <clp/ir/LogEventDeserializer.hpp>
#include <clp/ir/types.hpp>
#include <clp/TimestampPattern.hpp>
#include <emscripten/val.h>

#include <clp_ffi_js/ir/LogEventWithFilterData.hpp>
#include <clp_ffi_js/ir/StreamReader.hpp>
#include <clp_ffi_js/ir/StreamReaderDataContext.hpp>

namespace clp_ffi_js::ir {
using clp::ir::four_byte_encoded_variable_t;
using UnstructuredIrDeserializer = clp::ir::LogEventDeserializer<four_byte_encoded_variable_t>;
using UnstructuredLogEvents = LogEvents<UnstructuredLogEvent>;

/**
 * Class to deserialize and decode Zstd-compressed CLP unstructured IR streams, as well as format
 * decoded log events.
 */
class UnstructuredIrStreamReader : public StreamReader {
public:
    // Destructor
    ~UnstructuredIrStreamReader() override = default;

    // Disable copy constructor and assignment operator
    UnstructuredIrStreamReader(UnstructuredIrStreamReader const&) = delete;
    auto operator=(UnstructuredIrStreamReader const&) -> UnstructuredIrStreamReader& = delete;

    // Define default move constructor
    UnstructuredIrStreamReader(UnstructuredIrStreamReader&&) = default;
    // Delete move assignment operator since it's also disabled in `clp::ir::LogEventDeserializer`.
    auto operator=(UnstructuredIrStreamReader&&) -> UnstructuredIrStreamReader& = delete;

    /**
     * @param zstd_decompressor A decompressor for an IR stream, where the read head of the stream
     * is just after the stream's encoding type.
     * @param data_array The array backing `zstd_decompressor`.
     * @return The created instance.
     * @throw ClpFfiJsException if any error occurs.
     */
    [[nodiscard]] static auto create(
            std::unique_ptr<ZstdDecompressor>&& zstd_decompressor,
            clp::Array<char> data_array
    ) -> UnstructuredIrStreamReader;

    [[nodiscard]] auto get_ir_stream_type() const -> StreamType override {
        return StreamType::Unstructured;
    }

    [[nodiscard]] auto get_num_events_buffered() const -> size_t override;

    [[nodiscard]] auto get_filtered_log_event_map() const -> FilteredLogEventMapTsType override;

    void filter_log_events(LogLevelFilterTsType const& log_level_filter) override;

    /**
     * @see StreamReader::deserialize_stream
     *
     * After the stream has been exhausted, it will be deallocated.
     *
     * @return @see StreamReader::deserialize_stream
     */
    [[nodiscard]] auto deserialize_stream() -> size_t override;

    [[nodiscard]] auto decode_range(size_t begin_idx, size_t end_idx, bool use_filter) const
            -> DecodedResultsTsType override;

    [[nodiscard]] auto get_log_event_idx_by_timestamp(clp::ir::epoch_time_ms_t timestamp
    ) -> LogEventIdxTsType override;

private:
    // Constructor
    explicit UnstructuredIrStreamReader(
            StreamReaderDataContext<UnstructuredIrDeserializer>&& stream_reader_data_context
    );

    // Variables
    UnstructuredLogEvents m_encoded_log_events;
    std::unique_ptr<StreamReaderDataContext<UnstructuredIrDeserializer>>
            m_stream_reader_data_context;
    FilteredLogEventsMap m_filtered_log_event_map;
    clp::TimestampPattern m_ts_pattern;
};
}  // namespace clp_ffi_js::ir

#endif  // CLP_FFI_JS_IR_UNSTRUCTUREDIRSTREAMREADER_HPP
