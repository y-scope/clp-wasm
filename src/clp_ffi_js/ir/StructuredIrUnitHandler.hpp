#ifndef CLP_FFI_JS_IR_STRUCTUREDIRUNITHANDLER_HPP
#define CLP_FFI_JS_IR_STRUCTUREDIRUNITHANDLER_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <clp/ffi/ir_stream/decoding_methods.hpp>
#include <clp/ffi/KeyValuePairLogEvent.hpp>
#include <clp/ffi/SchemaTree.hpp>
#include <clp/ir/types.hpp>
#include <clp/time_types.hpp>

#include <clp_ffi_js/constants.hpp>
#include <clp_ffi_js/ir/LogEventWithFilterData.hpp>

namespace clp_ffi_js::ir {
using schema_tree_node_id_t = std::optional<clp::ffi::SchemaTree::Node::id_t>;

/**
 * Class that implements the `clp::ffi::ir_stream::IrUnitHandlerInterface` to buffer log events and
 * determine the schema-tree node IDs of the log level and timestamp kv-pairs.
 */
class StructuredIrUnitHandler {
public:
    // Constructors
    /**
     * @param deserialized_log_events The vector in which to store deserialized log events.
     * @param log_level_key Key name of schema-tree node that contains the authoritative log level.
     * @param timestamp_key Key name of schema-tree node that contains the authoritative timestamp.
     */
    StructuredIrUnitHandler(
            std::shared_ptr<std::vector<LogEventWithFilterData<StructuredLogEvent>>>
                    deserialized_log_events,
            std::string log_level_key,
            std::string timestamp_key
    )
            : m_log_level_key{std::move(log_level_key)},
              m_timestamp_key{std::move(timestamp_key)},
              m_deserialized_log_events{std::move(deserialized_log_events)} {}

    // Methods implementing `clp::ffi::ir_stream::IrUnitHandlerInterface`.
    /**
     * Buffers the log event with filter data extracted.
     * @param log_event
     * @return IRErrorCode::IRErrorCode_Success
     */
    [[nodiscard]] auto handle_log_event(StructuredLogEvent&& log_event
    ) -> clp::ffi::ir_stream::IRErrorCode;

    /**
     * Dummy implementation that does nothing but conforms to the interface.
     * @param utc_offset_old
     * @param utc_offset_new
     * @return IRErrorCode::IRErrorCode_Success
     */
    [[nodiscard]] static auto handle_utc_offset_change(
            [[maybe_unused]] clp::UtcOffset utc_offset_old,
            [[maybe_unused]] clp::UtcOffset utc_offset_new
    ) -> clp::ffi::ir_stream::IRErrorCode;

    /**
     * Saves the node's ID if it corresponds to events' authoritative log level or timestamp
     * kv-pair.
     * @param schema_tree_node_locator
     * @return IRErrorCode::IRErrorCode_Success
     */
    [[nodiscard]] auto handle_schema_tree_node_insertion(
            clp::ffi::SchemaTree::NodeLocator schema_tree_node_locator
    ) -> clp::ffi::ir_stream::IRErrorCode;

    /**
     * Dummy implementation that does nothing but conforms to the interface.
     * @return IRErrorCode::IRErrorCode_Success
     */
    [[nodiscard]] static auto handle_end_of_stream() -> clp::ffi::ir_stream::IRErrorCode;

private:
    // Methods
    /**
     * @param id_value_pairs
     * @return `LogLevel::LogLevelNone` if `m_log_level_node_id` is unset, the node has no value, or
     * the node's value is not an integer or string.
     * @return `LogLevel` from node with id `m_log_level_node_id` otherwise.
     */
    [[nodiscard]] auto get_log_level(StructuredLogEvent::NodeIdValuePairs const& id_value_pairs
    ) const -> LogLevel;

    /**
     * @param id_value_pairs
     * @return 0 if `m_timestamp_node_id` is unset, the node has no value, or the node's value is
     * not an integer.
     * @return Timestamp from node with ID `m_timestamp_node_id` otherwise.
     */
    [[nodiscard]] auto get_timestamp(StructuredLogEvent::NodeIdValuePairs const& id_value_pairs
    ) const -> clp::ir::epoch_time_ms_t;

    // Variables
    std::string m_log_level_key;
    std::string m_timestamp_key;

    clp::ffi::SchemaTree::Node::id_t m_current_node_id{clp::ffi::SchemaTree::cRootId};

    schema_tree_node_id_t m_log_level_node_id;
    schema_tree_node_id_t m_timestamp_node_id;

    // TODO: Technically, we don't need to use a `shared_ptr` since the parent stream reader will
    // have a longer lifetime than this class. Instead, we could use `gsl::not_null` once we add
    // `gsl` into the project.
    std::shared_ptr<std::vector<LogEventWithFilterData<StructuredLogEvent>>>
            m_deserialized_log_events;
};
}  // namespace clp_ffi_js::ir

#endif  // CLP_FFI_JS_IR_STRUCTUREDIRUNITHANDLER_HPP
