/**
 * Set of tools to manage real-time communication between the server and the client.
 */

#pragma once

#include <cstdint>

namespace wvb
{
    /** Wrapper around the RTC library, design for virtual reality streaming. */
    class VRStream
    {
      private:
        struct Data;
        Data *m_data;

      public:
        VRStream();
        ~VRStream();
        VRStream(const VRStream &) = delete;

        [[nodiscard]] inline bool is_valid() const noexcept { return m_data != nullptr; }
    };

} // namespace wvb