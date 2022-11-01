#include "wvb_client/client.h"


namespace wvb::client
{
    // =======================================================================================
    // =                                       Structs                                       =
    // =======================================================================================

    struct Client::Data
    {
        uint16_t port;
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    Client::Client(uint16_t port) : m_data(new Data {port}) {}

    Client::~Client()
    {
        if (m_data != nullptr)
        {
            delete m_data;
            m_data = nullptr;
        }
    }

} // namespace wvb::Client