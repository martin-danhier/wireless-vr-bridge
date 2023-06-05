#pragma once

#include <wvb_client/structs.h>
#include <wvb_common/benchmark.h>
#include <wvb_common/macros.h>
#include <wvb_common/vr_structs.h>

#include <memory>

namespace wvb
{
    class IVideoDecoder;
    struct IOBuffer;
}

namespace wvb::client
{
    /**
     * A VRSystem represents the interface to the VR environment.
     * It takes care of session management and tracking, as well as the entire video pipeline,
     * including decoding and rendering.
    */
    class VRSystem
    {
        PIMPL_CLASS(VRSystem);

      private:
        bool get_pose(int64_t xr_time, TrackingState &out_tracking_state) const;

      public:
        explicit VRSystem(std::shared_ptr<rtp::RTPClock> rtp_clock, std::shared_ptr<ClientMeasurementBucket> measurements_bucket);

        void init(const ApplicationInfo &app_info);
        void shutdown();
        void set_decoder(const std::shared_ptr<IVideoDecoder> &video_decoder);
        bool push_frame_data(const uint8_t *data, size_t size, uint32_t frame_index, bool end_of_stream, uint32_t timestamp, uint32_t pose_timestamp, uint32_t last_packet_received_timestamp, bool save_frame) const;
        [[nodiscard]] VRSystemSpecs specs() const;
        [[nodiscard]] uint64_t      ntp_epoch() const;
        void                        soft_shutdown();

        void               handle_events();
        bool               get_next_tracking_state(TrackingState &out_tracking_state) const;
        bool               init_decoder();
        [[nodiscard]] bool new_frame(ClientFrameTimeMeasurements &frame_execution_time);
        void               render(ClientFrameTimeMeasurements &frame_execution_time);
        uint32_t get_decoder_frame_delay() const;

        bool save_frame_if_needed(IOBuffer &image) const;
    };
} // namespace wvb::client