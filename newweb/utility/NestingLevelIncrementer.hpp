
#ifndef NestingLevelIncrementer_hpp
#define NestingLevelIncrementer_hpp

#include "common.hpp"
#include <boost/noncopyable.hpp>

class NestingLevelIncrementer : public boost::noncopyable
{
    DISALLOW_NEW();

public:
    explicit NestingLevelIncrementer(unsigned& nestingLevel)
        : m_nestingLevel(&nestingLevel)
    {
        ++(*m_nestingLevel);
    }

    ~NestingLevelIncrementer()
    {
        --(*m_nestingLevel);
    }

private:
    unsigned* m_nestingLevel;
};

#endif
