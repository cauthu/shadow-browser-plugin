#ifndef net_config_hpp
#define net_config_hpp



class NetConfig
{
public:
    NetConfig(const in_addr_t& socks5_addr,
              const in_port_t& socks5_port
        )
        : socks5_addr_(socks5_addr), socks5_port_(socks5_port)
    {}

    NetConfig()
        : NetConfig(0, 0)
    {}

    const in_addr_t& socks5_addr() const { return socks5_addr_; }
    const in_port_t& socks5_port() const { return socks5_port_; }

    void set_socks5_addr(const in_addr_t& a) { socks5_addr_ = a; }
    void set_socks5_port(const in_port_t& p) { socks5_port_ = p; }

private:

    in_addr_t socks5_addr_;
    in_port_t socks5_port_;
    
};

#endif /* end net_config_hpp */
