#include "pinter_controller.h"

#include <array>
#include <cstdio>

#include "console_controller_internal.h"
#include "date_time_math.h"
#include "pinter_store.h"

namespace pinter_controller
{

namespace cci = console_controller::console_controller_internal;

namespace
{

// The runtime catalogue stores only scheduling data for actual brew packs.
// Shop-only fields and glass/bundle products are deliberately omitted here.
constexpr std::array<PinterBrewTiming, kPinterBrewCatalogueCount> kPinterBrewCatalogue = {{
    {"Adnams Ghost Ship Remixed", 8U, 5U, 6U, 3U, 0U},
    {"After Midnight", 10U, 7U, 7U, 3U, 0U},
    {"Ancestor's", 8U, 5U, 6U, 3U, 0U},
    {"Appalachian Mountain Brewery", 9U, 5U, 7U, 3U, 0U},
    {"Black Magic Hour", 8U, 7U, 7U, 5U, 0U},
    {"BrewDog Elvis Juice Remixed", 9U, 5U, 7U, 3U, 0U},
    {"BrewDog Hazy Jane Remixed", 9U, 5U, 7U, 3U, 0U},
    {"BrewDog Punk IPA Remixed", 9U, 5U, 7U, 3U, 0U},
    {"Brewgooder Hazy IPA Remixed", 7U, 5U, 6U, 3U, 0U},
    {"Dark Matter", 5U, 7U, 4U, 3U, 0U},
    {"Deep Shade", 13U, 9U, 11U, 7U, 0U},
    {"En Casa", 10U, 10U, 7U, 5U, 0U},
    {"En Casa Lime", 10U, 10U, 7U, 5U, 0U},
    {"Fourpure Citrus IPA Remixed", 8U, 6U, 6U, 4U, 0U},
    {"Golden Grove", 5U, 7U, 4U, 3U, 0U},
    {"Great Lakes Burning River Remixed", 9U, 5U, 7U, 3U, 0U},
    {"Guinness 'Dublin Porter, Brewers Edition'", 6U, 4U, 4U, 3U, 0U},
    {"Hopewell", 10U, 10U, 8U, 7U, 0U},
    {"Inner Circle", 7U, 7U, 5U, 3U, 0U},
    {"Iron Maiden's Trooper Remixed", 7U, 5U, 5U, 3U, 0U},
    {"Lagunitas Sumpin' Easy Remixed", 9U, 7U, 8U, 5U, 0U},
    {"Lemon & Lime Hard Seltzer", 7U, 7U, 5U, 5U, 0U},
    {"Pear With Me", 9U, 5U, 7U, 3U, 0U},
    {"Public House", 5U, 7U, 4U, 3U, 0U},
    {"Razz", 7U, 5U, 5U, 3U, 0U},
    {"Shadow & Cream", 5U, 7U, 4U, 3U, 0U},
    {"Snap", 10U, 10U, 8U, 4U, 0U},
    {"Space Hopper", 7U, 7U, 5U, 3U, 0U},
    {"Space Hopper West Coast Edition", 7U, 7U, 5U, 3U, 0U},
    {"Stars & Stripes", 5U, 7U, 4U, 3U, 0U},
    {"Summer Haze", 9U, 5U, 7U, 3U, 0U},
    {"Sunlit", 10U, 15U, 8U, 4U, 0U},
    {"Waltham Forest", 8U, 3U, 6U, 2U, 0U},
    {"Whole Nine Yards", 8U, 3U, 6U, 2U, 0U},
    {"Yeastie Boys Bigmouth Remixed", 7U, 5U, 6U, 3U, 0U},
}};

static_assert(kPinterBrewCatalogue.size() <= 255U,
              "Pinter brew catalogue indices must fit in ConsoleState");

// Set by Pinter mutations, consumed by flush_pending_save(). The actual flash
// write is deferred out of the button-event call chain -- see
// console_controller.h's flush_pending_pinter_save() doc comment for why.
bool g_pinter_save_pending = false;

/// @brief Returns a valid catalogue index even if older state contains stale data.
size_t brew_catalogue_index(uint8_t brew_index)
{
    if (brew_index >= kPinterBrewCatalogue.size())
    {
        return kDefaultPinterBrewIndex;
    }

    return brew_index;
}

} // namespace

const PinterBrewTiming& brew_timing(uint8_t brew_index)
{
    return kPinterBrewCatalogue[brew_catalogue_index(brew_index)];
}

size_t brew_catalogue_count()
{
    return kPinterBrewCatalogue.size();
}

uint8_t recommended_total_days(const PinterBrewTiming& brew)
{
    return static_cast<uint8_t>(brew.recommended_brewing_days + brew.recommended_conditioning_days);
}

uint8_t minimum_total_days(const PinterBrewTiming& brew)
{
    return static_cast<uint8_t>(brew.minimum_brewing_days + brew.minimum_conditioning_days);
}

uint8_t list_page_count(size_t item_count)
{
    if (item_count == 0U)
    {
        return 1U;
    }

    return static_cast<uint8_t>(
        (item_count + (kPinterBrewListVisibleCount - 1U)) / kPinterBrewListVisibleCount);
}

void clamp_list_page(uint8_t& page_index, size_t item_count)
{
    const uint8_t page_count = list_page_count(item_count);
    if (page_index >= page_count)
    {
        page_index = static_cast<uint8_t>(page_count - 1U);
    }
}

const char* state_selection_text(PinterState state)
{
    switch (state)
    {
    case PinterState::Idle:
        return "Idle";
    case PinterState::Brewing:
        return "Brew";
    case PinterState::ColdCrash:
        return "Crash";
    case PinterState::Conditioning:
        return "Cond";
    case PinterState::Ready:
        return "Ready";
    case PinterState::Consumed:
        return "Done";
    }

    return "-";
}

uint8_t brew_dock_count(const ConsoleState& console_state)
{
    return pinter_scheduling::brew_dock_count(console_state.pinters);
}

uint8_t fridge_count(const ConsoleState& console_state)
{
    return pinter_scheduling::fridge_count(console_state.pinters);
}

pinter_scheduling::SummaryCounts summary_counts(const ConsoleState& console_state)
{
    return pinter_scheduling::summarize(console_state.pinters);
}

PinterStatus& selected(ConsoleState& console_state)
{
    if (console_state.selected_pinter_index >= console_state.pinters.size())
    {
        console_state.selected_pinter_index = 0U;
    }
    return console_state.pinters[console_state.selected_pinter_index];
}

const PinterStatus& selected_const(const ConsoleState& console_state)
{
    const uint8_t index = console_state.selected_pinter_index < console_state.pinters.size()
                              ? console_state.selected_pinter_index
                              : 0U;
    return console_state.pinters[index];
}

bool has_pending_cold_crash(const ConsoleState& console_state)
{
    return pinter_scheduling::has_pending_cold_crash(selected_const(console_state));
}

uint32_t current_event_day(const ConsoleState& console_state)
{
    if (!console_state.time_status.synced || console_state.time_status.local_epoch_day == 0U)
    {
        return 0U;
    }

    return console_state.time_status.local_epoch_day;
}

/// @details Stage advancement is a manual event the user triggers, not
/// something the firmware does automatically at the target day -- this is
/// advisory anticipation for the user, not a deadline being enforced, so an
/// overdue stage just clamps to "0d" rather than showing a negative count.
void format_countdown_text(const ConsoleState& console_state, const PinterStatus& pinter, char* out,
                           size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    out[0] = '\0';

    const uint32_t today = current_event_day(console_state);
    const uint32_t target_day = pinter_scheduling::current_stage_target_day(pinter);
    if (today == 0U || target_day == 0U)
    {
        return;
    }

    const uint32_t days_remaining = (target_day > today) ? (target_day - today) : 0U;
    const date_time_math::DateTimeParts target = date_time_math::civil_from_epoch_day(target_day);
    const char* month = (target.month >= 1 && target.month <= 12)
                            ? date_time_math::kMonthAbbreviations[static_cast<size_t>(target.month - 1)]
                            : "---";
    std::snprintf(out, out_size, "%uD - RDY %02d %s", static_cast<unsigned>(days_remaining),
                  target.day, month);
}

const char* build_slot_softkey_label(const ConsoleState& console_state, SoftKeyId key,
                                     const PinterStatus& pinter)
{
    const char* state_text = state_selection_text(pinter.state);
    if (pinter.state == PinterState::Idle || pinter.state == PinterState::Consumed)
    {
        return cci::build_selection_softkey_label(key, pinter.label.data(),
                                                                           state_text);
    }

    const PinterBrewTiming& brew = brew_timing(pinter.brew_index);
    char value[40] = {};
    std::snprintf(value, sizeof(value), "%s %s", state_text, brew.name);

    char countdown_text[24] = {};
    format_countdown_text(console_state, pinter, countdown_text, sizeof(countdown_text));
    if (countdown_text[0] == '\0')
    {
        return cci::build_selection_softkey_label(key, pinter.label.data(),
                                                                           value);
    }

    size_t buffer_capacity = 0U;
    char* buffer = cci::dynamic_softkey_label_buffer(key, buffer_capacity);
    char title_upper[24] = {};
    cci::build_uppercase_title(pinter.label.data(), title_upper,
                                                        sizeof(title_upper));
    std::snprintf(buffer, buffer_capacity, "%s\n[%s]\n[%s]", title_upper, value, countdown_text);
    return buffer;
}

const char* build_home_softkey_label(SoftKeyId key, const ConsoleState& console_state)
{
    size_t buffer_capacity = 0U;
    char* buffer = cci::dynamic_softkey_label_buffer(key, buffer_capacity);
    const pinter_scheduling::SummaryCounts counts = summary_counts(console_state);
    std::snprintf(buffer, buffer_capacity, "PINTER\n[%uB, %uC, %uR]",
                  static_cast<unsigned>(counts.brewing), static_cast<unsigned>(counts.conditioning),
                  static_cast<unsigned>(counts.ready));
    return buffer;
}

const char* build_catalogue_item_label(SoftKeyId key, uint8_t brew_index)
{
    const PinterBrewTiming& brew = brew_timing(brew_index);
    char timing_text[16] = {};
    std::snprintf(timing_text, sizeof(timing_text), "R%u M%u",
                  static_cast<unsigned>(recommended_total_days(brew)),
                  static_cast<unsigned>(minimum_total_days(brew)));
    return cci::build_selection_softkey_label(key, brew.name, timing_text);
}

const char* build_days_label(SoftKeyId key, const char* title, uint8_t days)
{
    char day_text[8] = {};
    std::snprintf(day_text, sizeof(day_text), "%ud", static_cast<unsigned>(days));
    return cci::build_selection_softkey_label(key, title, day_text);
}

bool can_start(const ConsoleState& console_state)
{
    const PinterStatus& pinter = selected_const(console_state);
    return pinter.state == PinterState::Idle &&
           pinter_scheduling::can_start(brew_dock_count(console_state));
}

bool primary_action_enabled(const ConsoleState& console_state)
{
    return pinter_scheduling::primary_action_enabled(
        selected_const(console_state), brew_dock_count(console_state), fridge_count(console_state));
}

const char* build_primary_action_label(SoftKeyId key, const ConsoleState& console_state)
{
    size_t buffer_capacity = 0U;
    char* buffer = cci::dynamic_softkey_label_buffer(key, buffer_capacity);
    const PinterStatus& pinter = selected_const(console_state);

    switch (pinter.state)
    {
    case PinterState::Idle:
        if (brew_dock_count(console_state) >= kPinterBrewDockCapacity)
        {
            std::snprintf(buffer, buffer_capacity, "START\n[NO DOCK]");
            return buffer;
        }
        std::snprintf(buffer, buffer_capacity, "START");
        return buffer;
    case PinterState::Brewing:
        if (has_pending_cold_crash(console_state))
        {
            std::snprintf(buffer, buffer_capacity, "COLD\nCRASH");
            return buffer;
        }
        if (fridge_count(console_state) >= kPinterFridgeCapacity)
        {
            std::snprintf(buffer, buffer_capacity, "FRIDGE\n[FULL]");
            return buffer;
        }
        std::snprintf(buffer, buffer_capacity, "FRIDGE");
        return buffer;
    case PinterState::ColdCrash:
        if (fridge_count(console_state) >= kPinterFridgeCapacity)
        {
            std::snprintf(buffer, buffer_capacity, "FRIDGE\n[FULL]");
            return buffer;
        }
        std::snprintf(buffer, buffer_capacity, "FRIDGE");
        return buffer;
    case PinterState::Conditioning:
        std::snprintf(buffer, buffer_capacity, "READY");
        return buffer;
    case PinterState::Ready:
        std::snprintf(buffer, buffer_capacity, "DRINK");
        return buffer;
    case PinterState::Consumed:
        std::snprintf(buffer, buffer_capacity, "CLEAN");
        return buffer;
    }

    std::snprintf(buffer, buffer_capacity, "-");
    return buffer;
}

/// @details The R1 softkey label only has room for a two-line hint (e.g.
/// "FRIDGE\n[FULL]"), which is easy to press past without registering why
/// nothing happened. This puts the same reason somewhere unmissable: the
/// page's own centre body, which is otherwise blank on the main Pinter page.
void update_block_reason(ConsoleState& console_state)
{
    auto& reason = console_state.pinter_block_reason;
    reason.fill('\0');

    const PinterStatus& pinter = selected_const(console_state);
    const uint8_t dock_count = brew_dock_count(console_state);
    const uint8_t fridge_slots_used = fridge_count(console_state);

    switch (pinter.state)
    {
    case PinterState::Idle:
        if (dock_count >= kPinterBrewDockCapacity)
        {
            std::snprintf(reason.data(), reason.size(), "DOCK FULL (%u/%u) - FREE A SLOT TO START",
                          static_cast<unsigned>(dock_count),
                          static_cast<unsigned>(kPinterBrewDockCapacity));
        }
        break;
    case PinterState::Brewing:
        if (!has_pending_cold_crash(console_state) && fridge_slots_used >= kPinterFridgeCapacity)
        {
            std::snprintf(reason.data(), reason.size(), "FRIDGE FULL (%u/%u) - FREE A SLOT",
                          static_cast<unsigned>(fridge_slots_used),
                          static_cast<unsigned>(kPinterFridgeCapacity));
        }
        break;
    case PinterState::ColdCrash:
        if (fridge_slots_used >= kPinterFridgeCapacity)
        {
            std::snprintf(reason.data(), reason.size(), "FRIDGE FULL (%u/%u) - FREE A SLOT",
                          static_cast<unsigned>(fridge_slots_used),
                          static_cast<unsigned>(kPinterFridgeCapacity));
        }
        break;
    case PinterState::Conditioning:
    case PinterState::Ready:
    case PinterState::Consumed:
        break;
    }
}

bool select_slot(ConsoleState& console_state, uint8_t index)
{
    if (index >= console_state.pinters.size())
    {
        return false;
    }

    if (console_state.selected_pinter_index == index)
    {
        return false;
    }

    console_state.selected_pinter_index = index;
    return true;
}

bool change_list_page(ConsoleState& console_state, int direction)
{
    if (console_state.active_page != MenuPage::PinterSelectBrew)
    {
        return false;
    }

    const uint8_t page_count = list_page_count(kPinterBrewCatalogue.size());
    const int next_page = static_cast<int>(console_state.pinter_catalogue_page_index) + direction;
    if (next_page < 0 || next_page >= static_cast<int>(page_count))
    {
        return false;
    }

    console_state.pinter_catalogue_page_index = static_cast<uint8_t>(next_page);
    return true;
}

/// @details Picking is on the fly: whatever recipe you have a pack for in hand
/// right now, chosen from the full catalogue, with no pre-planned reservation
/// step in between.
bool prepare_start(ConsoleState& console_state, uint8_t brew_index)
{
    if (!can_start(console_state) || brew_index >= kPinterBrewCatalogue.size())
    {
        return false;
    }

    const PinterBrewTiming& brew = brew_timing(brew_index);
    console_state.pinter_pending_brew_index = brew_index;
    console_state.pinter_pending_brewing_days = brew.recommended_brewing_days;
    console_state.pinter_pending_cold_crash_days = 0U;
    console_state.pinter_pending_conditioning_days = brew.recommended_conditioning_days;
    console_state.active_page = MenuPage::PinterStartTiming;
    return true;
}

bool select_list_item(ConsoleState& console_state, uint8_t visible_index)
{
    if (visible_index >= kPinterBrewListVisibleCount ||
        console_state.active_page != MenuPage::PinterSelectBrew)
    {
        return false;
    }

    const size_t brew_index = (static_cast<size_t>(console_state.pinter_catalogue_page_index) *
                               kPinterBrewListVisibleCount) +
                              visible_index;
    if (brew_index >= kPinterBrewCatalogue.size())
    {
        return false;
    }

    return prepare_start(console_state, static_cast<uint8_t>(brew_index));
}

bool set_pending_timing(ConsoleState& console_state, bool minimum)
{
    const PinterBrewTiming& brew = brew_timing(console_state.pinter_pending_brew_index);
    console_state.pinter_pending_brewing_days =
        minimum ? brew.minimum_brewing_days : brew.recommended_brewing_days;
    console_state.pinter_pending_conditioning_days =
        minimum ? brew.minimum_conditioning_days : brew.recommended_conditioning_days;
    return true;
}

bool adjust_pending_days(uint8_t& value, int direction, uint8_t minimum, uint8_t maximum)
{
    const int next_value = static_cast<int>(value) + direction;
    if (next_value < static_cast<int>(minimum) || next_value > static_cast<int>(maximum))
    {
        return false;
    }

    value = static_cast<uint8_t>(next_value);
    return true;
}

bool confirm_start(ConsoleState& console_state)
{
    if (!can_start(console_state))
    {
        return false;
    }

    PinterStatus& pinter = selected(console_state);
    pinter.state = PinterState::Brewing;
    pinter.brew_index = brew_catalogue_index(console_state.pinter_pending_brew_index);
    pinter.brew_start_day = current_event_day(console_state);
    pinter.cold_crash_start_day = 0U;
    pinter.conditioning_start_day = 0U;
    pinter.ready_day = 0U;
    pinter.planned_brewing_days = console_state.pinter_pending_brewing_days;
    pinter.planned_cold_crash_days = console_state.pinter_pending_cold_crash_days;
    pinter.planned_conditioning_days = console_state.pinter_pending_conditioning_days;
    pinter.cold_crash_used = false;

    console_state.active_page = MenuPage::Pinter;
    g_pinter_save_pending = true;
    return true;
}

/// @details Idle is not handled here: R1 navigates straight to the recipe
/// catalogue (SoftKeyRoute::GoPinterSelectBrew) instead of calling this,
/// since starting needs a recipe choice made on the fly first.
bool apply_primary_action(ConsoleState& console_state)
{
    PinterStatus& pinter = selected(console_state);
    if (pinter.state == PinterState::Idle || !primary_action_enabled(console_state))
    {
        return false;
    }

    const bool advanced = pinter_scheduling::advance_non_idle(
        pinter, current_event_day(console_state), fridge_count(console_state));
    if (advanced)
    {
        g_pinter_save_pending = true;
    }
    return advanced;
}

bool reset_selected(ConsoleState& console_state)
{
    const bool did_reset = pinter_scheduling::reset(selected(console_state), kDefaultPinterBrewIndex);
    if (did_reset)
    {
        g_pinter_save_pending = true;
    }
    return did_reset;
}

bool nudge_selected_day(ConsoleState& console_state, int delta)
{
    const bool nudged = pinter_scheduling::nudge_current_stage_days(selected(console_state), delta);
    if (nudged)
    {
        g_pinter_save_pending = true;
    }
    return nudged;
}

bool flush_pending_save(ConsoleState& console_state)
{
    if (!g_pinter_save_pending)
    {
        return false;
    }

    g_pinter_save_pending = false;
    return pinter_store::save(console_state.pinters);
}

} // namespace pinter_controller
