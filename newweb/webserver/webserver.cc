
#include <boost/lexical_cast.hpp>

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
    VLOG(2) << "web server got new client";

    Handler::UniquePtr handler(new Handler(std::move(channel), this));
    const auto id = handler->objId();
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

    std::set<uint16_t> listenports;

    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--port")) {
            uint16_t listenport = boost::lexical_cast<uint16_t>(argv[i+1]);
            listenports.insert(listenport);
        }
    }

    if (listenports.empty()) {
        listenports.insert(80);
    }

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "webserver starting...";

    std::unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    std::vector<Webserver::UniquePtr> webservers;

    for (const auto& listenport : listenports) {
        VLOG(2) << "listen port " << listenport;
        myio::TCPServer::UniquePtr tcpserver(
            new myio::TCPServer(evbase.get(), INADDR_ANY, listenport, nullptr));
        Webserver::UniquePtr webserver(new Webserver(std::move(tcpserver)));
        webservers.push_back(std::move(webserver));
    }

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";
    return 0;
}
