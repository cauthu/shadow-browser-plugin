#ifndef net_config_hpp
#define net_config_hpp



class NetConfig
{
public:
    NetConfig(const in_addr_t& socks5_addr,
              const in_port_t& socks5_port,
              const bool use_spdy,
              const bool use_tamaraw
        )
        : socks5_addr_(socks5_addr), socks5_port_(socks5_port)
        , use_spdy_(use_spdy), use_tamaraw_(use_tamaraw)
    {}

    const in_addr_t& socks5_addr() const { return socks5_addr_; }
    const in_port_t& socks5_port() const { return socks5_port_; }
    const bool& use_spdy() const { return use_spdy_; }
    const bool& use_tamaraw() const { return use_tamaraw_; }

private:

    const in_addr_t socks5_addr_;
    const in_port_t socks5_port_;
    const bool use_spdy_;
    const bool use_tamaraw_;
    
};

#endif /* end net_config_hpp */
