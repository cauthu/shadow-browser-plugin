#ifndef stream_server_hpp
#define stream_server_hpp

#include "object.hpp"
#include "stream_channel.hpp"

namespace myio
{

class StreamServer;

class StreamServerObserver
{
public:
    /* notify the user of a newly accepted channel. the channel has no
     * user -- the user should set itself */
    virtual void onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept = 0;

    /* the "errorcode" is from EVUTIL_SOCKET_ERROR() */
    virtual void onAcceptError(StreamServer*, int errorcode) noexcept = 0;
};

class StreamServer : public Object
{
public:
    typedef std::unique_ptr<StreamServer, /*folly::*/Destructor> UniquePtr;

    virtual bool start_listening() = 0;
    virtual bool start_accepting() = 0;
    virtual bool pause_accepting() = 0;
    virtual void set_observer(StreamServerObserver*) = 0;

    virtual bool is_listening() const = 0;
    virtual bool is_accepting() const = 0;
};

}

#endif /* stream_server_hpp */
