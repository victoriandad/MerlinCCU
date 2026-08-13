#include "time_manager.h"

// time_manager.cpp itself (the firmware implementation) includes
// pico/stdlib.h and isn't buildable here -- this host project has no Pico
// SDK toolchain. weather_normalisation.cpp calls
// time_manager::format_local_time_from_iso8601() to preserve the firmware's
// exact original behaviour (local-time conversion first, raw UTC-hour
// extraction as a fallback -- see weather_normalisation.cpp's
// format_hour_text()), so something must satisfy that symbol at link time
// for host_tests. Returning false always exercises exactly that documented
// fallback path, which is itself real, intended behaviour (see
// time_manager.h's doc comment: inputs without an explicit zone are treated
// as already-local and copied as `HH:MM`) -- not a shortcut around it.
namespace time_manager
{

bool format_local_time_from_iso8601(const char* iso_datetime, char* out, size_t out_size)
{
    (void)iso_datetime;
    (void)out;
    (void)out_size;
    return false;
}

} // namespace time_manager
