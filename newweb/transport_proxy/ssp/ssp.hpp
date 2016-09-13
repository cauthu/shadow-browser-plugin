#ifndef SSP_HPP
#define SSP_HPP

#include "../../utility/object.hpp"
#include "../../utility/stream_server.hpp"
#include "../../utility/tcp_channel.hpp"

#include "csp_handler.hpp"

class CSPHandler;

class ServerSideProxy : public Object
                      , public myio::StreamServerObserver
{
public:
    typedef std::unique_ptr<ServerSideProxy, /*folly::*/Destructor> UniquePtr;

    explicit ServerSideProxy(struct event_base* evbase,
                             myio::StreamServer::UniquePtr);

protected:

    virtual ~ServerSideProxy();

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*,
                            myio::StreamChannel::UniquePtr) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int) noexcept override;


    //////////////

    // the CSPHandler tells us it's closing down
    void _on_csp_handler_done(CSPHandler*);

    struct event_base* evbase_;
    /* server to listen for client connections */
    myio::StreamServer::UniquePtr stream_server_;

    std::map<uint32_t, CSPHandler::UniquePtr> csp_handlers_;
};


#endif /* SSP_HPP */
