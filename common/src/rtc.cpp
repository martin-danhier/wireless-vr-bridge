#include "wvb_common/rtc.h"

#include <rtc/rtc.hpp>
#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
typedef int SOCKET;
#endif

namespace wvb
{
    // =======================================================================================
    // =                                     Constants                                       =
    // =======================================================================================

    typedef uint8_t rtc_result_t;

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    struct VRStream::Data
    {
        rtc::PeerConnection peer_connection;
        SOCKET              udp_socket = 0;
    };

    // =======================================================================================
    // =                                      Utils                                          =
    // =======================================================================================

    void rtc_check(rtc_result_t result)
    {
        if (result != RTC_ERR_SUCCESS)
        {
            // Print error message
            switch (result)
            {
                case RTC_ERR_INVALID: std::cerr << "Invalid argument" << std::endl; break;
                case RTC_ERR_FAILURE: std::cerr << "Generic runtime failure" << std::endl; break;
                case RTC_ERR_NOT_AVAIL: std::cerr << "An element is not available at the moment" << std::endl; break;
                case RTC_ERR_TOO_SMALL: std::cerr << "An user-provided buffer is too small" << std::endl; break;
                default: std::cerr << "Unknown error" << std::endl; break;
            }

            // Throw exception
            throw std::runtime_error("RTC error");
        }
    }

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    VRStream::VRStream() : m_data(new Data)
    {
        m_data->peer_connection.onStateChange([](auto state) { std::cout << "State changed to " << state << "\n"; });
    }

    VRStream::~VRStream()
    {
        if (m_data != nullptr)
        {
            delete m_data;
            m_data = nullptr;
        }
    }
} // namespace wvb