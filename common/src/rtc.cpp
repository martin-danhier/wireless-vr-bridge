#include "wvb_common/rtc.h"

#include <rtc/datachannel.hpp>

//#include <cstdint>
//#include <uvgrtp/lib.hh>
//
//namespace wvb
//{
//    // =======================================================================================
//    // =                                 Structs and classes                                 =
//    // =======================================================================================
//
//    struct RTCSession::Data
//    {
//        uvgrtp::context  context;
//        uvgrtp::session *session = nullptr;
//    };
//
//    // =======================================================================================
//    // =                                   Implementation                                    =
//    // =======================================================================================
//
//
//    RTCSession::RTCSession(const char *peer_ip) : m_data(new Data)
//    {
//        // Create session
//        m_data->session = m_data->context.create_session(peer_ip);
//
//    }
//
//    RTCSession::~RTCSession()
//    {
//        if (m_data != nullptr)
//        {
//            // Delete session
//            m_data->context.destroy_session(m_data->session);
//
//            delete m_data;
//            m_data = nullptr;
//        }
//    }
//} // namespace wvb