#include "wvb_common/sctp.h"

//#include <iostream>
//#include <rawrtcc.h>
//#include <rawrtcdc.h>
//
//
//namespace wvb
//{
//
//    // =======================================================================================
//    // =                                 Structs and classes                                 =
//    // =======================================================================================
//
//    struct SCTPContext::Data
//    {
//        // DTLS transport
//        // rawrtc_dtls_transport* dtls_transport = nullptr;
//
//        rawrtc_sctp_transport *transport = nullptr;
//    };
//
//    // =======================================================================================
//    // =                                   Implementation                                    =
//    // =======================================================================================
//
//    enum rawrtc_code timer_handler(bool const on, uint_fast16_t const interval)
//    {
//        if (on)
//        {
//            // Start a timer that calls `rawrtcdc_timer_tick` every `interval`
//            // milliseconds.
//        }
//        else
//        {
//            // Stop the timer.
//        }
//
//        return RAWRTC_CODE_SUCCESS;
//    }
//
//    SCTPContext::SCTPContext()
//    {
//        auto error = rawrtcdc_init(true, timer_handler);
//        if (error)
//        {
//            std::cerr << "RawRTC initialisation failed: " << rawrtc_code_to_str(error);
//            throw std::runtime_error("RawRTC initialisation failed");
//        }
//
//        // Create transport context
//        rawrtc_sctp_transport_context context = {
//            .trace_packets = false,
//        };
//
//        // Create transport
//        // error = rawrtc_sctp_transport_create_from_external(&m_data->transport,
//        //                                                    &context,
//        //                                                    1000,
//        //                                                    data_channel_handler,
//        //                                                    state_change_handler,
//        //                                                    nullptr);
//        // if (error)
//        // {
//        //     std::cerr << "RawRTC SCTP transport creation failed: " << rawrtc_code_to_str(error);
//        //     throw std::runtime_error("RawRTC SCTP transport creation failed");
//        // }
//    }
//
//    SCTPContext::~SCTPContext()
//    {
//        if (m_data != nullptr)
//        {
//            // Close transport
//            if (m_data->transport != nullptr)
//            {
//                rawrtc_sctp_transport_stop(m_data->transport);
//                m_data->transport = nullptr;
//            }
//
//            delete m_data;
//            m_data = nullptr;
//        }
//    }
//} // namespace wvb
