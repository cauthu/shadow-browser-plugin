
#include <string>
#include <vector>
#include <set>
#include <boost/lexical_cast.hpp>



#include "../utility/tcp_server.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"

#include "webserver.hpp"
#include "handler.hpp"

using std::vector;
using std::string;
using std::set;
using std::pair;
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


struct MyConfig
{
    MyConfig()
  {
    }

  std::set<uint16_t> listenports;
};

static void
set_my_config(MyConfig& conf,
              const vector<pair<string, string> >& name_value_pairs)
{
    for (auto nv_pair : name_value_pairs) {
        const auto& name = nv_pair.first;
        const auto& value = nv_pair.second;

        if (name == "port") {
          conf.listenports.insert(boost::lexical_cast<uint16_t>(value));
	}

        else {
            // ignore other args
        }
    }
}

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();
    common::init_easylogging();

    START_EASYLOGGINGPP(argc, argv);

    MyConfig conf;

    bool found_conf_name = false;
    string found_conf_value;
    vector<pair<string, string> > name_value_pairs;
    auto rv = common::get_cmd_line_name_value_pairs(argc, (const char**)argv,
                                                  found_conf_name, found_conf_value,
                                                  name_value_pairs);
    CHECK(rv == 0);

    if (found_conf_name) {
        name_value_pairs.clear();
        LOG(INFO) << "configuring using config file. other command-line options are ignored.";
        rv = common::get_config_name_value_pairs(found_conf_value.c_str(),
                                                 name_value_pairs);
        CHECK(rv == 0);
    }

    set_my_config(conf, name_value_pairs);

    if (conf.listenports.empty()) {
        conf.listenports.insert(80);
    }

    LOG(INFO) << "webserver starting...";

    std::unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    std::vector<Webserver::UniquePtr> webservers;

    for (const auto& listenport : conf.listenports) {
        LOG(INFO) << "listening on port " << listenport;
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
