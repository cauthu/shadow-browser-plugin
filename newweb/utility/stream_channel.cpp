
#include "stream_channel.hpp"
#include "easylogging++.h"

namespace myio
{

StreamChannel::StreamChannel(StreamChannelObserver* observer)
    : observer_(observer)
    , read_size_hint_(-1)
{}

void
StreamChannel::set_read_size_hint(int len)
{
    CHECK_GE(len, -1);
    CHECK_NE(len, 0) << "not yet implemented";
    read_size_hint_ = len;
}

}
