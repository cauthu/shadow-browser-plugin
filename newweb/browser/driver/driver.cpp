
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <rapidjson/document.h>



#include "../../utility/tcp_channel.hpp"
#include "../../utility/common.hpp"
#include "utility/ipc/transport_proxy/gen/combined_headers"

#include "driver.hpp"


using myio::TCPChannel;
using myipc::GenericIpcChannel;
using std::string;
using std::set;
namespace json = rapidjson;

#define _LOG_PREFIX(inst) << "driver= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



Driver::Driver(struct event_base* evbase,
               const string& page_models_list_file,
               const string& browser_proxy_mode,
               const uint16_t tproxy_ipc_port,
               const uint16_t renderer_ipc_port)
    : evbase_(evbase)
    , using_tproxy_(tproxy_ipc_port > 0)
    , tproxy_ipc_ch_ready_(false)
    , browser_proxy_mode_(browser_proxy_mode)
    , state_(State::INITIAL)
    , loadnum_(0)
{

    logself(INFO) << "using page models listed in " << page_models_list_file;
    _read_page_models_file(page_models_list_file);

    page_model_rand_idx_gen_.reset(
        new boost::variate_generator<boost::mt19937, boost::uniform_int<> >(
            boost::mt19937(std::time(0)), boost::uniform_int<>(0, page_models_.size() - 1)));

    _reset_this_page_load_info();

    if (using_tproxy_) {
        logself(INFO) << "connect to tproxy ipc port: " << tproxy_ipc_port;
        myio::TCPChannel::UniquePtr tcpch1(
            new myio::TCPChannel(evbase_, common::getaddr("localhost"),
                                 tproxy_ipc_port, nullptr));
        tproxy_ipc_ch_.reset(
            new GenericIpcChannel(
                evbase_,
                std::move(tcpch1),
                boost::bind(&Driver::_tproxy_on_ipc_msg, this, _1, _2, _3, _4),
                boost::bind(&Driver::_tproxy_on_ipc_ch_status, this, _1, _2)));
    }

    logself(INFO) << "connect to renderer ipc port: " << renderer_ipc_port;
    myio::TCPChannel::UniquePtr tcpch2(
        new myio::TCPChannel(evbase_, common::getaddr("localhost"),
                             renderer_ipc_port, nullptr));
    renderer_ipc_ch_.reset(
        new GenericIpcChannel(
            evbase_,
            std::move(tcpch2),
            boost::bind(&Driver::_renderer_on_ipc_msg, this, _1, _2, _3, _4),
            boost::bind(&Driver::_renderer_on_ipc_ch_status, this, _1, _2)));

    page_load_timeout_timer_.reset(
        new Timer(evbase_, true,
                  boost::bind(&Driver::_on_page_load_timeout, this, _1)));

    grace_period_timer_.reset(
        new Timer(evbase_, true,
                  boost::bind(&Driver::_on_grace_period_timer_fired, this, _1)));

    think_time_timer_.reset(
        new Timer(evbase_, true,
                  boost::bind(&Driver::_on_think_time_timer_fired, this, _1)));

    static const int lowerbound_thinktime_ms = 20*1000;
    static const int upperbound_thinktime_ms = 60*1000;
    think_time_rand_gen_.reset(
        new boost::variate_generator<boost::mt19937, boost::uniform_real<double> >(
            boost::mt19937(std::time(0)), boost::uniform_real<double>(
                lowerbound_thinktime_ms, upperbound_thinktime_ms)));
    logself(INFO) << "picking uniform thinktimes in range [" << lowerbound_thinktime_ms
                  << ", " << upperbound_thinktime_ms << "]";
}

Driver::~Driver()
{
}

void
Driver::_read_page_models_file(const string& page_models_list_file)
{
    std::ifstream infile(page_models_list_file, std::ifstream::in);
    if (!infile.good()) {
        LOG(FATAL) << "error: can't read page models list file";
    }

    string line;
    set<string> page_names;
    set<string> page_model_fpaths;

    while (std::getline(infile, line)) {
        vlogself(1) << "line: [" << line << "]";
        if (line.length() == 0 || line.at(0) == '#') {
            /* empty lines and lines beginining with '#' are
               ignored */
            continue;
        }

        std::istringstream iss(line);
        string token;
        std::getline(iss, token, ' '); // get rid of page name
        const auto page_name = token;
        auto ret = page_names.insert(page_name);
        CHECK(ret.second) << "duplicated page name [" << page_name << "]";

        std::getline(iss, token, ' '); // now get the model file path
        boost::algorithm::trim(token);
        CHECK(token.length() > 0);
        const auto page_model_fpath = token;
        ret = page_model_fpaths.insert(page_model_fpath);
        CHECK(ret.second) << "duplicated page model [" << page_model_fpath << "]";

        logself(INFO) << "page \"" << page_name << "\", model \"" << page_model_fpath << "\"";

        // sanity test that the file at least contains a json object
        json::Document doc;
        const auto rv = common::get_json_doc_from_file(page_model_fpath.c_str(), doc);
        CHECK(rv && doc.IsObject());

        page_models_.push_back(make_pair(page_name, page_model_fpath));
    }

    if (page_models_.empty()) {
        logself(FATAL) << "no page models to load";
    }

#ifndef IN_SHADOW

    if (page_models_.size() != 1) {
        logself(FATAL) << "outside shadow, should specify exactly one page to load";
    }

#endif

}

void
Driver::_on_grace_period_timer_fired(Timer* timer)
{
    vlogself(2) << "begin";

    CHECK_EQ(timer, grace_period_timer_.get());

    logself(INFO) << "done grace period";

    CHECK_EQ(state_, State::GRACE_PERIOD_AFTER_DOM_LOAD_EVENT);

    CHECK_NE(this_page_load_info_.page_load_status_,
             PageLoadStatus::PENDING);

    _report_result(this_page_load_info_.page_load_status_,
                   this_page_load_info_.ttfb_ms_);

#ifndef IN_SHADOW

    // running outside shadow, so exit after one page load
    CHECK(0) << "need testing";
    CHECK_EQ(loadnum_, 1);
    logself(INFO) << "exiting";

#endif

    _reset_this_page_load_info();

    state_ = State::THINKING;

    const auto think_time_ms = (*think_time_rand_gen_)();
    logself(INFO) << "start thinking for " << think_time_ms << " ms";
    think_time_timer_->start(think_time_ms);

    vlogself(2) << "done";
}

void
Driver::_on_page_load_timeout(Timer* timer)
{
    vlogself(2) << "begin";

    CHECK_EQ(timer, page_load_timeout_timer_.get());

    logself(WARNING) << "page load has timed out";

    CHECK_EQ(state_, State::LOADING_PAGE);

    if (using_tproxy_) {
        _tproxy_stop_defense(false);
    }

    auto& tpli = this_page_load_info_;

    CHECK_EQ(tpli.page_load_status_, PageLoadStatus::PENDING);

    tpli.page_load_status_ = PageLoadStatus::TIMEDOUT;

    _report_result(tpli.page_load_status_, 0);

#ifndef IN_SHADOW

    // running outside shadow, so exit after one page load
    CHECK(0) << "need testing";
    CHECK_EQ(loadnum_, 1);
    logself(INFO) << "exiting";

#endif

    _reset_this_page_load_info();

    state_ = State::THINKING;

    const auto think_time_ms = (*think_time_rand_gen_)();
    logself(INFO) << "start thinking for " << think_time_ms << " ms";
    think_time_timer_->start(think_time_ms);

    vlogself(2) << "done";
}

void
Driver::_on_think_time_timer_fired(Timer* timer)
{
    vlogself(2) << "begin";

    CHECK_EQ(timer, think_time_timer_.get());

    logself(INFO) << "done thinking";

    // done thinkin, so prepare another round of loading
    CHECK_EQ(state_, State::THINKING);

    _renderer_reset();

    vlogself(2) << "done";
}

void
Driver::_tproxy_on_ipc_ch_status(GenericIpcChannel*,
                                 GenericIpcChannel::ChannelStatus status)
{
    switch (status) {
    case GenericIpcChannel::ChannelStatus::READY: {
        tproxy_ipc_ch_ready_ = true;
        _tproxy_maybe_establish_tunnel();
        break;
    }

    case GenericIpcChannel::ChannelStatus::CLOSED: {
        logself(FATAL) << "to do";
        break;
    }

    default:
        logself(FATAL) << "not reached";
        break;
    }
}

void
Driver::_renderer_on_ipc_ch_status(GenericIpcChannel*,
                                 GenericIpcChannel::ChannelStatus status)
{
    switch (status) {
    case GenericIpcChannel::ChannelStatus::READY: {
        _renderer_reset();
        break;
    }

    case GenericIpcChannel::ChannelStatus::CLOSED: {
        logself(FATAL) << "to do";
        break;
    }

    default:
        logself(FATAL) << "not reached";
        break;
    }
}
