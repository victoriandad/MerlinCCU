#include "config_manager.h"

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
namespace config_manager
{

const RuntimeConfig& settings()
{
    static RuntimeConfig config = {};
    return config;
}

} // namespace config_manager
