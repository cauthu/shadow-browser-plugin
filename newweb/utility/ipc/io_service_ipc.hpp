#ifndef io_service_ipc_hpp
#define io_service_ipc_hpp

#include <folly/io/async/DelayedDestruction.h>
#include <memory>

#include "../stream_server.hpp"
#include "../json_stream_channel.hpp"

using myio::StreamServer;
using myio::StreamChannel;

namespace myipc { namespace ioservice {

/*
 * IPC related to IO process/service. is meant to be used with the
 * json stream channel
 */

static const uint16_t service_port = 12345;


enum message_type : uint16_t
{
    /* client -> server msgs */
    HELLO,
    FETCH,
    CHANGE_PRIORITY,


    /* server -> client msgs */
    REQ_DATA,
    REQ_FINISH,

};


} // end namespace ioservice
} // end namespace myipc

#endif /* io_service_ipc_hpp */
