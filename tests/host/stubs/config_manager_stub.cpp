#include "config_manager.h"

#include "config_persistence.h"

// config_manager.cpp itself (the firmware implementation) includes
// hardware/flash.h, hardware/sync.h, and pico/stdlib.h and isn't buildable
// here -- this host project has no Pico SDK toolchain. screen_banners.cpp
// (called by every page's render) and a couple of Status subpages call
// config_manager::settings() to read the live RuntimeConfig, so something
// must satisfy that symbol at link time for host_tests. A default-
// constructed RuntimeConfig is a reasonable stand-in for golden-image
// rendering tests: those pages read a handful of config fields (HA
// host/token presence, etc.) purely for text formatting, not for the
// layout/geometry these tests actually check.
//
// save() mirrors the real implementation's observable contract (issue #78):
// it updates the in-memory settings immediately via the same
// config_persistence::sanitize_settings() the real config_manager.cpp uses,
// deliberately omitting only the flash write (flush_pending_save() isn't
// stubbed, since nothing here calls it). That's enough for
// settings_controller.cpp's tests, which only ever observe state through
// settings() after a save(), the same as the real firmware does before its
// next flush_pending_save().
namespace config_manager
{

namespace
{
RuntimeConfig g_settings = {};
} // namespace

const RuntimeConfig& settings()
{
    return g_settings;
}

bool save(const RuntimeConfig& new_settings)
{
    g_settings = config_persistence::sanitize_settings(new_settings);
    return true;
}

} // namespace config_manager
