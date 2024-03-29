#ifndef tcp_server_hpp
#define tcp_server_hpp

#include <event2/listener.h>

#include "stream_server.hpp"

namespace myio
{

class TCPServer : public StreamServer
{
public:
    typedef std::unique_ptr<TCPServer, /*folly::*/Destructor> UniquePtr;

    /* "port" should be in host byte order */
    explicit TCPServer(struct event_base*,
                       const in_addr_t& addr, const in_port_t& port,
                       StreamServerObserver*,
                       const bool start_listening=true);

    virtual bool start_listening() override;
    virtual bool start_accepting() override;
    virtual bool pause_accepting() override;
    virtual void set_observer(myio::StreamServerObserver*) override;

    virtual bool is_listening() const override { return listening_ ;}
    virtual bool is_accepting() const override { return state_ == ServerState::ACCEPTING; }

protected:

    virtual ~TCPServer() = default;

    void on_conn_accepted(struct evconnlistener *listener,
                          int fd, struct sockaddr *addr, int len);
    void on_accept_error(struct evconnlistener *listener, int errorcode);

    static void s_listener_acceptcb(struct evconnlistener *,
                                    int, struct sockaddr *, int, void *);
    static void s_listener_errorcb(struct evconnlistener *, void *);

    void _start_listening();

    ////////////////

    struct event_base* evbase_; // don't free
    StreamServerObserver* observer_; // don't free

    int fd_;
    const in_addr_t addr_;
    const in_port_t port_;

    enum class ServerState {
        INIT,
        ACCEPTING,
        PAUSED,
        CLOSED /* after either eof or error */
    } state_;

    bool listening_;

    std::unique_ptr<struct evconnlistener, void(*)(struct evconnlistener*)> evlistener_;
};

}

#endif /* tcp_server_hpp */
