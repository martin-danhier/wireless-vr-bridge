#include "wvb_common/server_shared_state.h"

namespace wvb
{
    // =======================================================================================
    // =                                    Driver events                                    =
    // =======================================================================================

    DriverEvents::DriverEvents(bool is_driver) : driver_state_changed(WVB_EVENT_DRIVER_STATE_CHANGED, is_driver) {}

    bool DriverEvents::poll(DriverEvent& event) const
    {
        if (driver_state_changed.is_signaled())
        {
            driver_state_changed.reset();
            event = DriverEvent::DRIVER_STATE_CHANGED;
            return true;
        }
        event = DriverEvent::NO_EVENT;
        return false;
    }

    // =======================================================================================
    // =                                     Server events                                   =
    // =======================================================================================

    ServerEvents::ServerEvents(bool is_server) : server_state_changed(WVB_EVENT_SERVER_STATE_CHANGED, is_server) {}

    bool ServerEvents::poll(ServerEvent& event) const
    {
        if (server_state_changed.is_signaled())
        {
            server_state_changed.reset();
            event = ServerEvent::SERVER_STATE_CHANGED;
            return true;
        }
        event = ServerEvent::NO_EVENT;
        return false;
    }
} // namespace wvb