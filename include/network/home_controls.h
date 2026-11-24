#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

/// @brief Home Assistant entities a CCU can switch or run from its Controls
/// page (#152).
/// @details HA's MerlinCCU integration publishes each unit's own list,
/// retained, to `<base>/<mac>/controls/list`, and republishes it when any
/// entity on it changes state. A key press goes to HA as
/// `<base>/<mac>/controls/press`; HA answers on `<base>/<mac>/controls/result`.
/// Schema: docs/mqtt-household-data.md. Pure logic, so host tests cover it.

/// @brief Whether a key switches its entity or runs it.
enum class ControlKind : uint8_t
{
    Toggle, ///< Lights, switches, fans, input booleans: on or off.
    Run,    ///< Scenes and scripts: started, with no state of their own.
};

/// @brief An entity's state as HA reports it.
enum class ControlState : uint8_t
{
    Off,
    On,
    Unavailable,
};

/// @brief Why the last press did not work.
enum class ControlFailure : uint8_t
{
    None,
    Refused, ///< HA answered that the action failed.
    NoReply, ///< HA did not answer within home_controls::kReplyTimeoutMs.
};

namespace home_controls
{

/// @brief Schema version this firmware reads; other versions are refused.
inline constexpr uint32_t kSchemaVersion = 1U;
/// @brief Most entities one unit's list may hold; HA's integration refuses a
/// longer list (MAX_CONTROLS in ha/custom_components/merlinccu/controls.py).
inline constexpr size_t kMaxControls = 24U;
/// @brief Characters kept of a name; HA trims names to this.
inline constexpr size_t kNameLength = 20U;
/// @brief Length of the list version HA sends with each list.
inline constexpr size_t kListIdLength = 8U;
/// @brief Largest list payload the CCU accepts, in bytes.
inline constexpr size_t kMaxListBytes = 2048U;
/// @brief Largest result payload the CCU accepts, in bytes.
inline constexpr size_t kMaxResultBytes = 160U;
/// @brief Keys per page: L1-L4 then R1-R4 (NAV-6).
inline constexpr uint8_t kControlsPerPage = 8U;
/// @brief How long a press waits for HA's answer before it counts as failed.
inline constexpr uint32_t kReplyTimeoutMs = 5000U;
/// @brief How long the page shows that a press failed.
inline constexpr uint32_t kFailureNoticeMs = 10000U;

} // namespace home_controls

/// @brief One entity on the list.
struct HomeControl
{
    std::array<char, home_controls::kNameLength + 1U> name;
    ControlKind kind;
    ControlState state;
};

/// @brief The Controls page's model: the list from HA, the link to it, and
/// the press in progress.
struct HomeControls
{
    /// A list has arrived since start-up.
    bool received;
    /// MQTT is connected and subscribed to the controls topics, so a press
    /// can reach HA.
    bool link_up;
    uint8_t count;
    std::array<HomeControl, home_controls::kMaxControls> items;
    /// Identifies the list a press refers to, so HA can refuse a press made
    /// against a list that has since changed.
    std::array<char, home_controls::kListIdLength + 1U> list_id;
    uint8_t page_index;
    /// Entry waiting for HA's answer, if any.
    std::optional<uint8_t> pending;
    uint32_t pending_since_ms;
    /// The entry's state when pressed: HA's list may report the change
    /// before HA's answer arrives, and then it must not be flipped again.
    ControlState pending_from;
    /// A press waiting for MQTT to send it.
    bool press_queued;
    uint8_t press_index;
    /// The last failure, shown for kFailureNoticeMs.
    ControlFailure failure;
    uint8_t failure_index;
    uint32_t failure_at_ms;
    /// Failures since start-up; the hook for a CONTROL FAILED alert.
    uint32_t failure_count;
};

namespace home_controls
{

/// @brief Which controls topic an incoming publish is.
enum class Topic : uint8_t
{
    List,
    Result,
};

/// @brief Returns which controls topic `topic` is, for this unit's topic
/// prefix `prefix` (`<base>/<mac>`).
std::optional<Topic> topic_for(std::string_view topic, std::string_view prefix);

/// @brief Replaces the list with one HA published.
/// @details A list with a new id ends any press in progress: its index may
/// now mean another entity. The page is clamped to the new length.
/// @return False, with the model unchanged, for a refused payload: another
/// schema version, no list id, or arrays that do not line up.
bool apply_list(const char* json, HomeControls& controls);

/// @brief Applies HA's answer to the press in progress.
/// @details A success ends the wait, and a toggle shows its new state until
/// HA's next list confirms it (unless that list came first). A failure is recorded for the page.
/// @return True when the model changed.
bool apply_result(const char* json, HomeControls& controls, uint32_t now_ms);

/// @brief Starts a press of entry `index`: queues it for MQTT and marks it
/// pending. False when it cannot act: no link, another press in progress, an
/// unavailable entity or a bad index.
bool press(HomeControls& controls, uint8_t index, uint32_t now_ms);

/// @brief Takes the queued press as its MQTT payload, `<list id>:<index>`.
/// @return False when nothing is queued or `out` is too small.
bool take_press(HomeControls& controls, char* out, size_t out_size);

/// @brief Records whether a press can reach HA. Losing the link ends a press
/// in progress without counting it as a failure.
/// @return True when the model changed.
bool set_link(HomeControls& controls, bool link_up);

/// @brief Times out an unanswered press and clears an old failure notice.
/// @return True when the model changed.
bool tick(HomeControls& controls, uint32_t now_ms);

/// @brief List pages for the current list; always at least one.
uint8_t page_count(const HomeControls& controls);

/// @brief The failure notice for the page, e.g. `HALL LIGHT: NO REPLY`, or
/// an empty string when there is none.
void failure_notice(const HomeControls& controls, char* out, size_t out_size);

} // namespace home_controls
