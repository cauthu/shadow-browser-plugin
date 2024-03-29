#ifndef socks5_connector_hpp
#define socks5_connector_hpp


#include <memory>
#include <string>

#include "object.hpp"
#include "stream_channel.hpp"
#include "folly/DelayedDestruction.h"


namespace myio
{

class Socks5Connector;

// interface
class Socks5ConnectorObserver
{
public:
    enum class ConnectResult
    {
        OK,
        
        ERR_FAIL,
        ERR_FAIL_TRANSPORT_EOF,
        ERR_FAIL_TRANSPORT_ERROR,
    };

    /* successfully connected to target */
    virtual void onSocksTargetConnectResult(Socks5Connector*, ConnectResult) noexcept = 0;
};

class Socks5Connector : public StreamChannelObserver
                      , public Object
{
public:
    typedef std::unique_ptr<Socks5Connector, Destructor> UniquePtr;

    /* the transport should already be connected to the proxy, and
     * zero bytes have been sent/received on it
     *
     * this object needs to itself as observer of the transport, so
     * once it's done, the user might need to once again set himself
     * as the observer of the transport
     *
     * the "port" is in HOST-byte order
     */
    Socks5Connector(StreamChannel::UniquePtr transport,
                    const in_addr_t target_host_addr, uint16_t port);
    Socks5Connector(StreamChannel::UniquePtr transport,
                    const char* target_host_name, uint16_t port);

    int start_connecting(Socks5ConnectorObserver*);

    /* return the underlying tranport */
    StreamChannel::UniquePtr release_transport() { return std::move(transport_); }  

private:

    virtual ~Socks5Connector() = default;

    /**** implement StreamChannel interface *****/
    virtual void onNewReadDataAvailable(StreamChannel*) noexcept override;
    virtual void onWrittenData(StreamChannel*) noexcept override {};
    virtual void onEOF(StreamChannel*) noexcept override;
    virtual void onError(StreamChannel*, int errorcode) noexcept override;

    void _consume_input();
    void _set_done_and_notify(Socks5ConnectorObserver::ConnectResult);

    enum class State {
        SOCKS5_NONE,
        SOCKS5_GREETING,
        SOCKS5_WRITE_REQUEST_NEXT,
        SOCKS5_READ_RESP_NEXT,
        SOCKS5_DONE
    } state_;

    StreamChannel::UniquePtr transport_;
    const in_addr_t target_host_addr_;
    const std::string target_host_name_;
    const uint16_t port_;
    Socks5ConnectorObserver* observer_;
};

}

#endif /* socks5_connector_hpp */
