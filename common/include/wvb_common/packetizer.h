#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace wvb
{
    /**
     * A packetizer is responsible for splitting an encoded into packets that can be sent over the network.
    */
    class IPacketizer
    {
      public:
        virtual ~IPacketizer() = default;

        virtual const char *name() const = 0;

        /**
         * Provide a frame to the packetizer.
         * If `last` is true, this data is considered the last of this frame.
         * The user thus is expected to provide a newer timestamp to subsequent calls to this function.
         *
         * The data is not copied, so the user is expected to keep it valid until the create_next_packet() function returns false.
         *
         * Frame ID, end of stream and save frame and pose timestamp are used by the client for measurements. They should be passed alongside
         * the frame data.
         */
        virtual void add_frame_data(const uint8_t *h264_data,
                                    size_t         h264_size,
                                    uint32_t       frame_id,
                                    bool           end_of_stream,
                                    uint32_t       rtp_sampling_timestamp,
                                    uint32_t       rtp_pose_timestamp,
                                    bool save_frame = false, // If set, the client will save the decoded frame and send it back to
                                                             // the server for PSNR calculation.
                                    bool last = true) = 0;

        /**
         * Computes and returns the next packet to send.
         * The size is unbounded, so large bitstreams can be sent as is through TCP.
         * For UDP, packetizers that limits the output to a limited MTU (typically 1500 bytes) need to be used.
         *
         * Returns true if there are other packets to send for this frame.
         * Typically called in a loop until it returns false.
         */
        virtual bool create_next_packet(const uint8_t **__restrict out_packet_data, size_t *__restrict out_size) = 0;
    };

    /**
     * A depacketizer is responsible for reassembling packets into a frame.
     *
     * It should be able to function efficiently in multiple threads. In particular, the add_packet() is called from the main thread, while
     * the receive_frame_data() is called from the rendering thread.
     *
     * Depending on the underlying network protocols, packets may or may not arrive in order, and there may be duplicates or missing packets.
    */
    class IDepacketizer
    {
      public:
        virtual ~IDepacketizer() = default;

        virtual const char *name() const = 0;

        /** Adds a new packet to the depacketizer.
         *
         * The m_packet data is copied into a larger buffer, so the user is free to reuse the provided buffer.
         */
        virtual void add_packet(const uint8_t *packet_data, size_t packet_size) = 0;

        /**
         * Returns a pointer to the reassembled frame data.
         * The data lives in the depacketizer's internal buffer.
         * It will remain valid until add_packet() or receive_frame_data() are called again.
         *
         * Returns true if it has a m_packet to return.
         */
        virtual bool receive_frame_data(const uint8_t **__restrict out_frame_data,
                                        size_t *__restrict out_frame_size,
                                        uint32_t *__restrict out_frame_id,
                                        bool *__restrict out_end_of_stream,
                                        uint32_t *__restrict out_rtp_sampling_timestamp,
                                        uint32_t *__restrict out_rtp_pose_timestamp,
                                        std::chrono::steady_clock::time_point *out_last_packet_received_timestamp,
                                        bool *__restrict out_save_frame) = 0;

        /**
         * Releases the frame data so that the buffer can be reused for new frames.
         * This function must be called once the data returned by receive_frame_data isn't used anymore.
         */
        virtual void release_frame_data() = 0;
    };
} // namespace wvb
