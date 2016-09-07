#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "../utility/stream_server.hpp"
#include "handler.hpp"

class Webserver : public Object
                , public myio::StreamServerObserver
                , public HandlerObserver
{
public:
    typedef std::unique_ptr<Webserver, /*folly::*/Destructor> UniquePtr;

    explicit Webserver(myio::StreamServer::UniquePtr);

private:

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*,
                            myio::StreamChannel::UniquePtr channel) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int errorcode) noexcept override;

    /****** HandlerObserver interface *****/
    virtual void onHandlerDone(Handler*) noexcept override;

    virtual ~Webserver();

    myio::StreamServer::UniquePtr stream_server_;
    std::map<uint32_t, Handler::UniquePtr> handlers_;
};

#endif /* WEBSERVER_HPP */
