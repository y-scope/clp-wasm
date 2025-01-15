#ifndef CLP_FFI_JS_IR_STRUCTUREDIRSTREAMREADER_HPP
#define CLP_FFI_JS_IR_STRUCTUREDIRSTREAMREADER_HPP

#include <cstddef>
#include <memory>
#include <optional>

#include <clp/Array.hpp>
#include <clp/ffi/ir_stream/Deserializer.hpp>
#include <clp/ffi/SchemaTree.hpp>
#include <clp/ir/types.hpp>
#include <emscripten/val.h>

#include <clp_ffi_js/ir/LogEventWithFilterData.hpp>
#include <clp_ffi_js/ir/StreamReader.hpp>
#include <clp_ffi_js/ir/StreamReaderDataContext.hpp>
#include <clp_ffi_js/ir/StructuredIrUnitHandler.hpp>

namespace clp_ffi_js::ir {
using schema_tree_node_id_t = std::optional<clp::ffi::SchemaTree::Node::id_t>;
using StructuredIrDeserializer = clp::ffi::ir_stream::Deserializer<StructuredIrUnitHandler>;
using StructuredLogEvents = LogEvents<StructuredLogEvent>;

/**
 * Class to deserialize and decode Zstd-compressed CLP structured IR streams, as well as format
 * decoded log events.
 */
class StructuredIrStreamReader : public StreamReader {
public:
    /**
     * @param zstd_decompressor A decompressor for an IR stream, where the read head of the stream
     * is just after the stream's encoding type.
     * @param data_array The array backing `zstd_decompressor`.
     * @param reader_options
     * @return The created instance.
     * @throw ClpFfiJsException if any error occurs.
     */
    [[nodiscard]] static auto create(
            std::unique_ptr<ZstdDecompressor>&& zstd_decompressor,
            clp::Array<char> data_array,
            ReaderOptions const& reader_options
    ) -> StructuredIrStreamReader;

    // Destructor
    ~StructuredIrStreamReader() override = default;

    // Disable copy constructor and assignment operator
    StructuredIrStreamReader(StructuredIrStreamReader const&) = delete;
    auto operator=(StructuredIrStreamReader const&) -> StructuredIrStreamReader& = delete;

    // Define default move constructor
    StructuredIrStreamReader(StructuredIrStreamReader&&) = default;
    // Delete move assignment operator since it's also disabled in `clp::ir::LogEventDeserializer`.
    auto operator=(StructuredIrStreamReader&&) -> StructuredIrStreamReader& = delete;

    [[nodiscard]] auto get_ir_stream_type() const -> StreamType override {
        return StreamType::Structured;
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
    explicit StructuredIrStreamReader(
            StreamReaderDataContext<StructuredIrDeserializer>&& stream_reader_data_context,
            std::shared_ptr<StructuredLogEvents> deserialized_log_events
    );

    // Variables
    std::shared_ptr<StructuredLogEvents> m_deserialized_log_events;
    std::unique_ptr<StreamReaderDataContext<StructuredIrDeserializer>> m_stream_reader_data_context;
    FilteredLogEventsMap m_filtered_log_event_map;
};
}  // namespace clp_ffi_js::ir

#endif  // CLP_FFI_JS_IR_STRUCTUREDIRSTREAMREADER_HPP
