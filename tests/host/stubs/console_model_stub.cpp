#include "console_model.h"

// console_model.cpp's real make_default_console_state() includes
// hardware/flash.h to populate ImageFootprintStatus from linker symbols
// (issue #71) and isn't buildable here -- this host project has no Pico SDK
// toolchain. console_controller.cpp's apply_softkey_route() calls it from the
// SoftKeyRoute::ResetConsoleState case, so something must satisfy the symbol
// at link time for host_tests (issue #78). Tests for the split-out
// *_controller.cpp modules build their own ConsoleState fixtures by hand
// (the same pattern test_golden_screens.cpp already uses), so a bare
// zero-initialized state -- rather than duplicating the real function's ~150
// lines of page/menu defaults, which would only drift from it over time --
// is a reasonable stand-in for the one code path that calls this.
void make_default_console_state(ConsoleState& out)
{
    out = ConsoleState{};
}
