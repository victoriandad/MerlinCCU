#include "softkey_builders.h"

#include "console_controller_internal.h"
#include "settings_controller.h"

namespace softkey
{

namespace
{

namespace cci = console_controller::console_controller_internal;

} // namespace

SoftKeyAction blank()
{
    return {"", SoftKeyRoute::None, false, false, SoftKeyRole::Blank};
}

SoftKeyAction navigate(const char* label, SoftKeyRoute route)
{
    return {label, route, true, false, SoftKeyRole::Navigate};
}

SoftKeyAction navigate(SoftKeyId key, const char* caption, const char* value, SoftKeyRoute route)
{
    return navigate(cci::build_selection_softkey_label(key, caption, value), route);
}

SoftKeyAction select(SoftKeyId key, const char* caption, const char* value, SoftKeyRoute route)
{
    return {cci::build_selection_softkey_label(key, caption, value), route, true, false,
            SoftKeyRole::Select};
}

SoftKeyAction select_editing(SoftKeyId key, const char* caption, const char* value)
{
    return {cci::build_selection_softkey_label(key, caption, value), SoftKeyRoute::None, false,
            true, SoftKeyRole::Select};
}

SoftKeyAction action(const char* verb, SoftKeyRoute route)
{
    return {verb, route, true, false, SoftKeyRole::Action};
}

SoftKeyAction action(SoftKeyId key, const char* verb, const char* value, SoftKeyRoute route)
{
    return action(cci::build_selection_softkey_label(key, verb, value), route);
}

SoftKeyAction toggle(SoftKeyId key, const char* caption, bool on, SoftKeyRoute route)
{
    return {cci::build_selection_softkey_label(key, caption,
                                               settings_controller::enabled_selection_text(on)),
            route, true, false, SoftKeyRole::Toggle};
}

SoftKeyAction view(SoftKeyId key, const char* caption, const char* value, SoftKeyRoute route)
{
    return {cci::build_selection_softkey_label(key, caption, value), route, true, false,
            SoftKeyRole::View};
}

SoftKeyAction step(const char* label, SoftKeyRoute route)
{
    return {label, route, true, false, SoftKeyRole::Step};
}

SoftKeyAction status(SoftKeyId key, const char* caption, const char* value)
{
    return status(cci::build_selection_softkey_label(key, caption, value));
}

SoftKeyAction status(const char* label)
{
    return {label, SoftKeyRoute::None, false, false, SoftKeyRole::Status};
}

SoftKeyAction option(const char* label, SoftKeyRoute route, bool selected)
{
    return {label, route, true, selected, SoftKeyRole::Option};
}

SoftKeyAction item(const char* label, SoftKeyRoute route, bool selected)
{
    return {label, route, true, selected, SoftKeyRole::Item};
}

} // namespace softkey
