#include "pinter_store.h"

// pinter_store.cpp itself (the firmware implementation) includes
// hardware/flash.h and isn't buildable here -- this host project has no Pico
// SDK toolchain. pinter_controller.cpp calls pinter_store::save() from
// flush_pending_save(), so something must satisfy that symbol at link time
// for host_tests (issue #78). Tests that care whether a save was requested
// exercise pinter_controller's own g_pinter_save_pending flag (via
// flush_pending_save()'s return value) rather than this stub, so a no-op
// success return is a faithful enough stand-in for the flash write itself.
namespace pinter_store
{

bool save(const std::array<PinterStatus, kPinterCount>& pinters)
{
    (void)pinters;
    return true;
}

} // namespace pinter_store
