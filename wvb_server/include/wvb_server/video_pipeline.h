#pragma once

#include <wvb_common/macros.h>
#include <wvb_common/server_shared_state.h>
#include <functional>

namespace wvb
{
    struct VRSystemSpecs;
    class IVideoEncoder;
} // namespace wvb

namespace wvb::server
{

    /**
     * The video pipeline of the server manages the processing of SteamVR video frames:
     *
     * 1. Given a shared resource handle for the backbuffer texture, copy it to a staging buffer
     * 2. Download the texture to a CPU buffer
     * 3. Encode the frame in a video codec
     * 4. Pack the frame in RTP packets
     * 5. Send those packets to the client.
     *
     * Encoding is designed to be flexible and support multiple codecs. Those variables parts are thus managed by a separate object,
     * the VideoEncoder. Each encoder handles the encoding and packing of the frame in RTP packets.
     */
    class VideoPipeline
    {
        PIMPL_CLASS(VideoPipeline);

      public:
        // Default constructor creates nullptr
        VideoPipeline() = default;

        /**
         *
         * @param video_encoder Encoder that should be used.
         */
        VideoPipeline(std::shared_ptr<ServerDriverSharedMemory> shared_memory,
                      std::shared_ptr<DriverEvents>             driver_events,
                      std::shared_ptr<ServerEvents>             server_events,
                      std::shared_ptr<IVideoEncoder>            video_encoder,
                      std::shared_ptr<ServerVideoSocket>        video_socket,
                      const VRSystemSpecs                      &specs,
                      uint64_t                                  ntp_epoch,
                      std::shared_ptr<ServerMeasurementBucket>  measurements,
                      std::function<void()> on_worker_thread_stopped);

        /** Should be called when the app starts running. */
        void start_worker_thread();
        void send_stop_signal();
        void send_kill_signal();
        void join();
    };
} // namespace wvb::server