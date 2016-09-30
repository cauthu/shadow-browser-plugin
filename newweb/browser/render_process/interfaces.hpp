#ifndef interfaces_hpp
#define interfaces_hpp

class ResourceMsgHandler
{
    /* implement this and tell ioserviceipcclient about yourself and
     * it will call you when it receives messages related to resource
     * loading */
public:

    virtual void handle_ReceivedResponse(const int& req_id) = 0;
    virtual void handle_DataReceived(const int& req_id, const size_t& length) = 0;
    virtual void handle_RequestComplete(const int& req_id, const bool success) = 0;
};

#endif /* interfaces_hpp */