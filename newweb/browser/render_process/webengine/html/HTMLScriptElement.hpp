#ifndef HTMLScriptElement_hpp
#define HTMLScriptElement_hpp


#include "../dom/Element.hpp"
#include "../page_model.hpp"

namespace blink {

class HTMLScriptElement : public Element
{
public:

    typedef std::unique_ptr<HTMLScriptElement, Destructor> UniquePtr;

    explicit HTMLScriptElement(const uint32_t instNum,
                               Document*,
                               const PageModel::ElementInfo& info);

    const bool& is_parser_blocking() const { return blocks_parser_; }
    const bool& exec_immediately() const { return exec_immediately_; }
    const bool& exec_async() const { return exec_async_; }

    const uint32_t run_scope_id() const { return run_scope_id_; }

    virtual void setResInstNum(const uint32_t& resInstNum) override;

protected:

    virtual ~HTMLScriptElement() = default;

    const bool blocks_parser_;

    /* exec_immediately_ and exec_async_ cannot be both true */
    const bool exec_immediately_;
    const bool exec_async_;

    const uint32_t run_scope_id_;
};

} //namespace

#endif
