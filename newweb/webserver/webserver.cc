
#include "../utility/tcp_server.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"

#include "webserver.hpp"
#include "handler.hpp"


using myio::StreamServer;
using myio::StreamChannel;


Webserver::Webserver(StreamServer::UniquePtr streamserver)
    : stream_server_(std::move(streamserver))
{
    stream_server_->set_observer(this);
    VLOG(2) << "tell stream server to start accepting";
    stream_server_->start_accepting();
}

void
Webserver::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    const auto id = channel->objId();
    VLOG(2) << "web server got new client, objid= " << id;
    Handler::UniquePtr handler(new Handler(std::move(channel), this));
    const auto ret = handlers_.insert(make_pair(id, std::move(handler)));
    CHECK(ret.second); // insist it was newly inserted
}

void
Webserver::onAcceptError(StreamServer*, int errorcode) noexcept
{
    LOG(WARNING) << "webserver has accept error: " << strerror(errorcode);
}

void
Webserver::onHandlerDone(Handler* handler) noexcept
{
    auto const id = handler->objId();
    CHECK(inMap(handlers_, id));
    handlers_.erase(id);
}

Webserver::~Webserver()
{
    LOG(FATAL) << "not reached";
}


INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "webserver starting...";

    std::unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    const uint16_t listenport = 80;

    VLOG(2) << "listen port " << listenport;

    /* ***************************************** */

    myio::TCPServer::UniquePtr tcpserver(
        new myio::TCPServer(evbase.get(), INADDR_ANY, listenport, nullptr));
    Webserver::UniquePtr webserver(new Webserver(std::move(tcpserver)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";
    return 0;
}
