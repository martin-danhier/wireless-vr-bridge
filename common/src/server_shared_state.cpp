#include "wvb_common/server_shared_state.h"

namespace wvb
{
    // =======================================================================================
    // =                                    Driver events                                    =
    // =======================================================================================

    DriverEvents::DriverEvents(bool is_driver)
        : driver_state_changed(WVB_EVENT_DRIVER_STATE_CHANGED, is_driver),
          new_present_info(WVB_EVENT_DRIVER_NEW_PRESENT_INFO, is_driver),
          new_measurements(WVB_EVENT_DRIVER_NEW_MEASUREMENTS, is_driver)
    {
    }

    bool DriverEvents::poll(DriverEvent &event) const
    {
        // Don't poll on new present info, it will be done in the pipeline thread

        if (driver_state_changed.is_signaled())
        {
            driver_state_changed.reset();
            event = DriverEvent::DRIVER_STATE_CHANGED;
            return true;
        }
        if (new_measurements.is_signaled())
        {
            new_measurements.reset();
            event = DriverEvent::NEW_MEASUREMENTS;
            return true;
        }
        event = DriverEvent::NO_EVENT;
        return false;
    }

    // =======================================================================================
    // =                                     Server events                                   =
    // =======================================================================================

    ServerEvents::ServerEvents(bool is_server)
        : server_state_changed(WVB_EVENT_SERVER_STATE_CHANGED, is_server),
          new_system_specs(WVB_EVENT_SERVER_SESSION_CREATED, is_server),
          frame_finished(WVB_EVENT_SERVER_FRAME_FINISHED, is_server),
          new_tracking_data(WVB_EVENT_SERVER_NEW_TRACKING_DATA, is_server),
          new_benchmark_data(WVB_EVENT_SERVER_NEW_BENCHMARK_DATA, is_server)
    {
    }

    bool ServerEvents::poll(ServerEvent &event) const
    {
        if (new_tracking_data.is_signaled())
        {
            new_tracking_data.reset();
            event = ServerEvent::NEW_TRACKING_DATA;
            return true;
        }
        if (server_state_changed.is_signaled())
        {
            server_state_changed.reset();
            event = ServerEvent::SERVER_STATE_CHANGED;
            return true;
        }
        if (new_system_specs.is_signaled())
        {
            new_system_specs.reset();
            event = ServerEvent::NEW_SYSTEM_SPECS;
            return true;
        }
        if (frame_finished.is_signaled())
        {
            frame_finished.reset();
            event = ServerEvent::FRAME_FINISHED;
            return true;
        }
        if (new_benchmark_data.is_signaled())
        {
            new_benchmark_data.reset();
            event = ServerEvent::NEW_BENCHMARK_DATA;
            return true;
        }
        event = ServerEvent::NO_EVENT;
        return false;
    }

} // namespace wvb