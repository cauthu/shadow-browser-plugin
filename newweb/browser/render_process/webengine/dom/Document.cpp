
#include <event2/event.h>
#include <strings.h>
#include <boost/bind.hpp>

#include "../../../../utility/easylogging++.h"
#include "../../../../utility/common.hpp"
#include "../../../../utility/folly/ScopeGuard.h"

#include "Document.hpp"
#include "../webengine.hpp"
#include "../html/HTMLScriptElement.hpp"
#include "../html/HTMLLinkElement.hpp"
#include "../html/HTMLImageElement.hpp"


using std::make_pair;
using std::shared_ptr;


#define _LOG_PREFIX(inst) << "doc= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



namespace blink {


Document::Document(struct ::event_base* evbase,
                   const uint32_t& instNum,
                   Webengine* webengine,
                   const PageModel* page_model,
                   ResourceFetcher* resource_fetcher)
    : EventTarget(instNum)
    , evbase_(evbase)
    , webengine_(webengine)
    , page_model_(page_model)
    , resource_fetcher_(resource_fetcher)
    , state_(DocumentState::INITIAL)
    , pendingStylesheets_(0)
    , executeScriptsWaitingForResourcesTimer_(
        new Timer(evbase, true,
                  boost::bind(&Document::_executeScriptsWaitingForResourcesTimerFired, this, _1)))
    , has_body_element_(false)
{
    CHECK_NOTNULL(evbase_);
    parser_.reset(
        new HTMLDocumentParser(page_model, this, webengine_, resource_fetcher_));
}

void
Document::load()
{
    CHECK_EQ(state_, DocumentState::INITIAL);
    state_ = DocumentState::LOADING;
    _load_main_resource();
}

void
Document::add_elem(const uint32_t& elemInstNum)
{
    PageModel::ElementInfo elem_info;

    vlogself(2) << "begin, elem:" << elemInstNum;

    CHECK(!inMap(elements_, elemInstNum));

    const auto rv = page_model_->get_element_info(elemInstNum, elem_info);
    CHECK(rv);

    Element* element = nullptr;
    SCOPE_EXIT {
        // should have cleared element before exiting this func
        CHECK(!element);
    };

    if (elem_info.tag == "script") {
        element = 
            new HTMLScriptElement(elemInstNum, this,
                                  elem_info.is_parser_blocking,
                                  elem_info.exec_immediately,
                                  elem_info.exec_async,
                                  elem_info.run_scope_id);
        HTMLScriptElement* script_elem = (HTMLScriptElement*)element;
        if (script_elem->is_parser_blocking()) {
            vlogself(2) << "this script blocks parser";
            parser_->set_parser_blocking_script(script_elem);
        }
    }

    else if (elem_info.tag == "link") {
        element = 
            new HTMLLinkElement(elemInstNum, this,
                                elem_info.rel,
                                elem_info.is_blocking_css);
    }

    else if (elem_info.tag == "img") {
        element =
            new HTMLImageElement(elemInstNum, this);
    }

    else if (elem_info.tag == "body") {
        CHECK(!has_body_element_);
        has_body_element_ = true;
        return;
    }

    else {
        logself(FATAL) << "element tag [" << elem_info.tag << "] not supported";
    }

    CHECK_NOTNULL(element);

    if (elem_info.initial_resInstNum) {
        vlogself(2) << "set its initial resource to res:"
                    << elem_info.initial_resInstNum;
        element->setResInstNum(elem_info.initial_resInstNum);
    }

    std::shared_ptr<Element> elem_shptr(
        element,
        [](Element* e) { e->destroy(); }
        );
    element = nullptr;

    const auto ret = elements_.insert(make_pair(elemInstNum, elem_shptr));
    CHECK(ret.second);

    vlogself(2) << "done";
}

void
Document::set_elem_res(const uint32_t elemInstNum, const uint32_t resInstNum)
{
    VLOG(2) << "begin, elem:" << elemInstNum << " res:" << resInstNum;

    shared_ptr<Element> element = elements_[elemInstNum];
    CHECK_NOTNULL(element.get());

    element->setResInstNum(resInstNum);

    VLOG(2) << "done";
}

bool
Document::isScriptExecutionReady() const
{
    return pendingStylesheets_ > 0;
}

void
Document::addPendingSheet(Element* element)
{
    ++pendingStylesheets_;
    vlogself(2) << "elem:" << element->instNum()
                << " increments pendingStylesheets_ to " << pendingStylesheets_;
}

void
Document::removePendingSheet(Element* element)
{
    CHECK_GT(pendingStylesheets_, 0);
    --pendingStylesheets_;
    vlogself(2) << "elem:" << element->instNum()
                << " decrements pendingStylesheets_ to " << pendingStylesheets_;

    if (pendingStylesheets_) {
        return;
    }

    _didLoadAllScriptBlockingResources();
}

void
Document::_didLoadAllScriptBlockingResources()
{
    struct timeval tv;
    // fire asap
    bzero(&tv, sizeof tv);
    executeScriptsWaitingForResourcesTimer_->start(&tv);
}

void
Document::_executeScriptsWaitingForResourcesTimerFired(Timer*)
{
    // for now assert that we only reach here if there's not pending
    // sheet. although real browser silently return if
    // pendingStylesheets_ > 0. because after the timer is scheduled
    // but before it fires, more blocking style sheets can be added
    // (e.g., by script)
    CHECK_EQ(pendingStylesheets_, 0);

    parser_->executeScriptsWaitingForResources();
}

void
Document::_load_main_resource()
{
    main_resource_ = resource_fetcher_->getMainResource();
    CHECK(main_resource_);
    // make sure it's not being loaded yet
    CHECK(!main_resource_->isFinished());
    CHECK(!main_resource_->isLoading());
    main_resource_->addClient(this);
    main_resource_->load();
}

void
Document::notifyFinished(Resource* resource, bool success)
{
    vlogself(2) << "begin, success= " << success;

    CHECK_EQ(main_resource_.get(), resource);

    CHECK(success) << "TODO: deal with failure of main resource";

    vlogself(2) << "pump the parser";

    parser_->finish_receive();
    parser_->pump_parser();

    vlogself(2) << "done";
}

void
Document::dataReceived(Resource* resource, size_t length)
{
    vlogself(2) << "begin, " << length << " more bytes";

    CHECK_EQ(main_resource_.get(), resource);

    vlogself(2) << "pump the parser";

    parser_->appendBytes(length);
    parser_->pump_parser();

    vlogself(2) << "done";
}

void
Document::responseReceived(Resource* resource)
{
    CHECK_EQ(main_resource_.get(), resource);
}


} // end namespace blink
