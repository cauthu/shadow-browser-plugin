#include "object.hpp"
#include "easylogging++.h"


static uint32_t
s_next_instNum(void)
{
    static uint32_t next = 0;
    CHECK_LT(next, 0x7FFFFFFF);
    return ++next;
}


Object::Object()
    : instNum_(s_next_instNum())
{}

const uint32_t&
Object::objId() const
{
    return instNum_;
}

void
Object::destroy()
{}
