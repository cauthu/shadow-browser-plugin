#ifndef DOMTimer_hpp
#define DOMTimer_hpp

#include "../../../../utility/object.hpp"
#include "../../../../utility/timer.hpp"
#include "../page_model.hpp"


namespace blink {

    class Webengine;

class DOMTimer final : public Timer
{
public:
    typedef std::unique_ptr<DOMTimer, Destructor> UniquePtr;

    explicit DOMTimer(struct ::event_base*,
                      Webengine*,
                      const PageModel::DOMTimerInfo&);

    const uint32_t& timerID() const { return timer_info_.timerID; }

private:

    virtual ~DOMTimer();

    virtual void _on_event_cb() override;

    const PageModel::DOMTimerInfo timer_info_;
    Webengine* webengine_;

    // which one are we going to execute at the next timer fired
    size_t next_fired_scope_idx_;
};

} // namespace blink

#endif // DOMTimer_hpp
