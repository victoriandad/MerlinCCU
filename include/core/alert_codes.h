#pragma once

#include <cstddef>
#include <cstdint>

/// @brief Every alert type the CCU can raise.
/// @details Each type has at most one entry in the active alert list, so the
/// list is sized from `Count` (NAV-6a). Adding a code here grows the list
/// with no other edit. Values are stored in `ActiveAlert::code`.
enum class AlertCode : uint8_t
{
    WifiDisconnected = 0,
    WifiAuthFailed,
    TimeNotSynced,
    HomeAssistantOffline,
    HomeAssistantUnauthorized,
    HomeAssistantEntityMissing,
    WeatherUnavailable,
    WeatherProviderWarning,
    WeatherTemperatureWarning,
    WeatherWindWarning,
    MqttOffline,
    KeypadLineFault,
    EnvironmentSensorFault,
    LocalPressureStormWarning,
    DisplayPipelineLag,
    ShareDataUnavailable,
    PinterScheduleConflict,
    AirTrafficUnavailable,
    Count,
};

/// @brief Number of alert types, and so the most alerts that can be active.
inline constexpr size_t kAlertCodeCount = static_cast<size_t>(AlertCode::Count);
