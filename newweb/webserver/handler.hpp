#ifndef HANDLER_HPP
#define HANDLER_HPP

#include <string>
#include <queue>
#include <set>

#include "../utility/object.hpp"
#include "../utility/stream_channel.hpp"


class Handler;

class HandlerObserver
{
public:
    virtual void onHandlerDone(Handler*) noexcept = 0;
};

class Handler : public Object
              , public myio::StreamChannelObserver
              , public myio::StreamChannelInputDropObserver
{
public:
    typedef std::unique_ptr<Handler, folly::DelayedDestruction::Destructor> UniquePtr;

    explicit Handler(myio::StreamChannel::UniquePtr channel,
                     HandlerObserver* observer);

private:

    virtual ~Handler();

    /********* StreamChannelObserver interface *************/
    virtual void onNewReadDataAvailable(myio::StreamChannel*) noexcept override;
    virtual void onEOF(myio::StreamChannel*) noexcept override;
    virtual void onError(myio::StreamChannel*, int errorcode) noexcept override;
    virtual void onWrittenData(myio::StreamChannel*) noexcept override {};

    /********* StreamChannelInputDropObserver interface *************/
    virtual void onCompleteInputDrop(myio::StreamChannel*, size_t) noexcept override;

    ///////////////////

    void _maybe_consume_input();

    myio::StreamChannel::UniquePtr channel_; // the underlying stream
    HandlerObserver* observer_;

    enum class HTTPReqState {
        HTTP_REQ_STATE_REQ_LINE,
        HTTP_REQ_STATE_HEADERS,
        HTTP_REQ_STATE_BODY
    } http_req_state_;

    struct RequestInfo
    {
        size_t resp_headers_size;
        size_t resp_body_size;
    };

    // of the current _request_ we're extracting from client (parsed
    // from content-length header)
    size_t remaining_req_body_length_;

    /* not yet complete requests. once a request is complete, should
     * remove it from here. each element is the "path" component of
     * the get request line, e.g., the "/index.html" of "GET
     * /index.html ...". submitted_req_queue_.front() is the request
     * actively being served. once we finish sending the response for
     * it, then should pop() it off the queue.
     */
    std::queue<RequestInfo> submitted_req_queue_;

    enum class HTTPRespState {
        HTTP_RSP_STATE_META,
        HTTP_RSP_STATE_BODY,
    } http_rsp_state_;

    uint16_t peer_port_;

    /* for debugging / asserting, for the current active response */
    size_t numRespBodyBytesExpectedToSend_;
    size_t numRespBytesSent_;
    size_t numBodyBytesRead_;

};

#endif /* HANDLER_HPP */
