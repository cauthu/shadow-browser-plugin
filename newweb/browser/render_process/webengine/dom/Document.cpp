
#include <event2/event.h>
#include <strings.h>
#include <boost/bind.hpp>

#include "../../../../utility/easylogging++.h"
#include "../../../../utility/common.hpp"
#include "../../../../utility/folly/ScopeGuard.h"

#include "Document.hpp"
#include "../webengine.hpp"
#include "../events/EventTypeNames.hpp"
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
    : EventTarget(instNum, webengine)
    , evbase_(evbase)
    , webengine_(webengine)
    , page_model_(page_model)
    , resource_fetcher_(resource_fetcher)
    , state_(ReadyState::Initial)
    , executeScriptsWaitingForResourcesTimer_(
        new Timer(evbase, true,
                  boost::bind(&Document::_executeScriptsWaitingForResourcesTimerFired, this, _1)))
    , has_body_element_(false)
    , finished_parsing_(false)
    , load_start_time_ms_(0)
{
    CHECK_NOTNULL(evbase_);

    PageModel::DocumentInfo doc_info;
    auto rv = page_model_->get_main_doc_info(doc_info);
    CHECK(rv);

    add_event_handling_scopes(doc_info.event_handling_scopes);

    vlogself(2) << "number of event handling scopes: "
                << doc_info.event_handling_scopes.size();

    parser_.reset(
        new HTMLDocumentParser(page_model, this, webengine_, resource_fetcher_));
}

void
Document::load()
{
    CHECK_EQ(state_, ReadyState::Initial);
    state_ = ReadyState::Loading;
    load_start_time_ms_ = common::gettimeofdayMs();
    _load_main_resource();
}

void
Document::setReadyState(ReadyState readyState)
{
    if (readyState == state_) {
        return;
    }

    state_ = readyState;

    switch (readyState) {
    case ReadyState::Initial:
    case ReadyState::Loading:
    case ReadyState::Interactive:
        break;
    case ReadyState::Complete:
        fireEventHandlingScopes(EventTypeNames::readystatechange_complete);
        break;
    }
}

void
Document::implicitClose()
{
    CHECK_EQ(state_, ReadyState::Complete);

    // in real webkit, this will be done by LocalDOMWindow, i.e., the
    // window, not the document, fires "load" event. but we'll
    // approximate it here
    fireEventHandlingScopes(EventTypeNames::load);
}

bool
Document::parsing() const
{
    return !finished_parsing_;
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
        element = new HTMLScriptElement(elemInstNum, this, elem_info);

        HTMLScriptElement* script_elem = (HTMLScriptElement*)element;
        if (script_elem->is_parser_blocking()) {
            vlogself(2) << "this script blocks parser";
            parser_->set_parser_blocking_script(script_elem);
        }
    }

    else if (elem_info.tag == "link") {
        element = new HTMLLinkElement(elemInstNum, this, elem_info);
    }

    else if (elem_info.tag == "img") {
        element = new HTMLImageElement(elemInstNum, this, elem_info);
    }

    else if (elem_info.tag == "body") {
        CHECK(!has_body_element_);
        has_body_element_ = true;
        webengine_->maybe_sched_INITIAL_render_update_scope();
        return;
    }

    else {
        logself(FATAL) << "element tag [" << elem_info.tag << "] not supported";
    }

    CHECK_NOTNULL(element);

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

    if (inMap(elements_, elemInstNum)) {
        shared_ptr<Element> element = elements_[elemInstNum];
        CHECK_NOTNULL(element.get());
        element->setResInstNum(resInstNum);
    } else {
        // this can happen because scripts can set diff elems
        // depending on their existence. but that is something we
        // cannot model yet
        LOG(WARNING) << "elem:" << elemInstNum << " does not (yet) exist";
    }

    VLOG(2) << "done";
}

bool
Document::isScriptExecutionReady() const
{
    return pendingStylesheets_.empty();
}

void
Document::addPendingSheet(Element* element)
{
    const auto elemInstNum = element->instNum();
    const auto it = pendingStylesheets_.find(elemInstNum);
    CHECK(it == pendingStylesheets_.end())
        << "elem:" << elemInstNum << " already a pending stylesheet";
    pendingStylesheets_.insert(elemInstNum);
    vlogself(2) << "elem:" << elemInstNum
                << " increments pendingStylesheets_ to " << pendingStylesheets_.size();
}

void
Document::removePendingSheet(Element* element)
{
    const auto elemInstNum = element->instNum();
    const auto it = pendingStylesheets_.find(elemInstNum);
    CHECK(it != pendingStylesheets_.end())
        << "elem:" << element->instNum() << " not a pending stylesheet";
    pendingStylesheets_.erase(it);
    vlogself(2) << "elem:" << elemInstNum
                << " decrements pendingStylesheets_ to " << pendingStylesheets_.size();

    if (pendingStylesheets_.size()) {
        return;
    }

    vlogself(2) << "schedule timer to execute scripts";
    _didLoadAllScriptBlockingResources();
}

void
Document::finishedParsing()
{
    const auto elapsed_ms = common::gettimeofdayMs() - load_start_time_ms_;
    logself(INFO) << "has finished parsing (after " << elapsed_ms << " ms)";
    finished_parsing_ = true;
    fireEventHandlingScopes(EventTypeNames::DOMContentLoaded);
    webengine_->finishedParsing();
}

void
Document::_didLoadAllScriptBlockingResources()
{
    if (!executeScriptsWaitingForResourcesTimer_->is_running()) {
        // fire asap
        static const uint32_t delayMs = 0;
        const auto rv = executeScriptsWaitingForResourcesTimer_->start(delayMs, false);
        CHECK(rv);
    }
}

void
Document::_executeScriptsWaitingForResourcesTimerFired(Timer*)
{
    // after the timer is scheduled but before it fires, more blocking
    // style sheets can be added (e.g., by script)
    if (pendingStylesheets_.empty()) {
        parser_->executeScriptsWaitingForResources();
    }
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
    /* the only resource we watch is our main resource */
    
    vlogself(2) << "begin, success= " << success;

    CHECK_EQ(main_resource_.get(), resource);

    CHECK(success) << "TODO: deal with failure of main resource";

    parser_->finish_receive();

    vlogself(2) << "done";
}

void
Document::dataReceived(Resource* resource, size_t length)
{
    vlogself(2) << "begin, " << length << " more bytes";

    CHECK_EQ(main_resource_.get(), resource);

    vlogself(2) << "pump the parser";

    parser_->appendBytes(length);
    parser_->pumpTokenizerIfPossible();

    vlogself(2) << "done";
}

void
Document::responseReceived(Resource* resource)
{
    CHECK_EQ(main_resource_.get(), resource);
}

} // end namespace blink
