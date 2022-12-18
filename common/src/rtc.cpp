#include "wvb_common/rtc.h"

#include <wvb_common/socket.h>

#include <rtc/rtc.hpp>

namespace wvb
{
    // =======================================================================================
    // =                                     Constants                                       =
    // =======================================================================================

    typedef int8_t rtc_result_t;

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    struct VRStream::Data
    {
        // Config
        SocketAddr remote_addr;
        // Objects
        std::shared_ptr<rtc::PeerConnection> peer_connection;
        UDPSocket           udp_socket;
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

    VRStream::VRStream(uint16_t local_port, SocketAddr remote_addr)
        : m_data(new Data {
            .udp_socket = UDPSocket(local_port),
        })
    {
        // Setup peer connection
        std::shared_ptr<rtc::PeerConnection> pc = std::make_shared<rtc::PeerConnection>();
        m_data->peer_connection = pc;
        
        // General callbacks
        pc->onStateChange([](rtc::PeerConnection::State state) {
            std::cout << "State changed: " << state << std::endl;
        });
        pc->onGatheringStateChange([pc](rtc::PeerConnection::GatheringState state
        ) {
            
        });
        
        
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