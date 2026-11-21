#pragma once

#include "console_model.h"

/// @brief Softkey maps for the Home menu and the Local Conditions page
/// (page_table.h), the two pages with no feature controller of their own.
namespace home_controller
{

/// @brief Home: one key per top-level section, some with a summary value.
void build_home_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys);

/// @brief Local Conditions: one reading key per measured quantity.
void build_local_conditions_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys);

} // namespace home_controller
