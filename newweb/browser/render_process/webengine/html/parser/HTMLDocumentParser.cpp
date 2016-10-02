
#include <vector>
#include <memory>

#include "HTMLDocumentParser.hpp"
#include "../HTMLScriptElement.hpp"
#include "../../webengine.hpp"

#include "../../../../../utility/NestingLevelIncrementer.hpp"
#include "../../../../../utility/easylogging++.h"
#include "../../../../../utility/folly/ScopeGuard.h"


using std::vector;
using std::shared_ptr;


#define _LOG_PREFIX(inst) << "parser= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



namespace blink {


HTMLDocumentParser::HTMLDocumentParser(const PageModel* page_model,
                                       Document* document,
                                       Webengine* webengine,
                                       ResourceFetcher* resource_fetcher)
    : page_model_(page_model)
    , document_(document)
    , webengine_(webengine)
    , resource_fetcher_(resource_fetcher)
    , parser_blocking_script_(nullptr)
    , num_bytes_received_(0)
    , done_received_(false)
    , num_bytes_parsed_(0)
    , num_preload_scanned_bytes_(0)
    , no_more_preload_scanning_necessary_(false)
    , element_loc_idx_(0)
    , is_executing_script_(false)
    , m_endWasDelayed(false)
    , m_pumpSessionNestingLevel(0)
{

    CHECK_NOTNULL(document_);
    CHECK_NOTNULL(webengine_);
    CHECK_NOTNULL(page_model_);

    CHECK(element_locations_.empty());
    page_model_->get_main_html_element_byte_offsets(element_locations_);
    CHECK(!element_locations_.empty());
}

void
HTMLDocumentParser::appendBytes(size_t len)
{
    vlogself(2) << "received so far= " << num_bytes_received_
                << " new bytes= " << len;
    CHECK(!done_received_);
    num_bytes_received_ += len;
}

void
HTMLDocumentParser::finish_receive()
{
    vlogself(2) << "finish receiving html";
    CHECK(!done_received_);
    done_received_ = true;

    attemptToEnd();
}

void
HTMLDocumentParser::attemptToEnd()
{
    vlogself(2) << "begin";

    // finish() indicates we will not receive any more data. If we are
    // waiting on an external script to load, we can't finish parsing
    // quite yet.

    if (shouldDelayEnd()) {
        vlogself(2) << "can't end yet. delaying";
        m_endWasDelayed = true;
        return;
    }

    prepareToStopParsing();
    vlogself(2) << "done";
}

bool
HTMLDocumentParser::hasParserBlockingScript() const
{
    return (parser_blocking_script_ != nullptr);
}

void
HTMLDocumentParser::executeScriptsWaitingForResources()
{
    vlogself(2) << "begin";

    CHECK(document_->isScriptExecutionReady());

    executeParsingBlockingScripts();

    if (!isWaitingForScripts()) {
        resumeParsingAfterScriptExecution();
    }

    vlogself(2) << "done";
}

void
HTMLDocumentParser::resumeParsingAfterScriptExecution()
{
    CHECK(!isExecutingScript());
    CHECK(!isWaitingForScripts());

    pump_parser();
    endIfDelayed();
}

void
HTMLDocumentParser::endIfDelayed()
{
    if (!m_endWasDelayed || shouldDelayEnd()) {
        return;
    }

    m_endWasDelayed = false;
    prepareToStopParsing();
}

void
HTMLDocumentParser::executeParsingBlockingScript()
{
    vlogself(2) << "begin";

    CHECK(!isExecutingScript());
    CHECK(document_->isScriptExecutionReady());
    CHECK(isPendingScriptReady(parser_blocking_script_));
    CHECK(parser_blocking_script_);

    HTMLScriptElement* script_elem = parser_blocking_script_;

    // clear the ptr before executing it
    parser_blocking_script_ = nullptr;
    _execute_script(script_elem->run_scope_id());

    vlogself(2) << "done";
}

void
HTMLDocumentParser::executeParsingBlockingScripts()
{
    vlogself(2) << "begin";

    while (hasParserBlockingScript()
           && isPendingScriptReady(parser_blocking_script_))
    {
        executeParsingBlockingScript();
    }

    vlogself(2) << "done";
}

void
HTMLDocumentParser::pump_parser()
{
    vlogself(2) << "begin, parsed= " << num_bytes_parsed_
                << " received= " << num_bytes_received_;

    NestingLevelIncrementer session(m_pumpSessionNestingLevel);

    while ((num_bytes_parsed_ < num_bytes_received_)
           && !hasParserBlockingScript())
    {
        // continue parsing

        vlogself(2) << "elem loc idx= " << element_loc_idx_;

        if (element_loc_idx_ == element_locations_.size()) {
            // no more elements to parse, so we just consume all the
            // available bytes ---- real browser has logic to stop
            // pause parsing, i.e., yield, even if there's more data
            // available, just to allow other stuff to run
            num_bytes_parsed_ = num_bytes_received_;
        } else {
            // REMEMBER that the location is byte offset, i.e.,
            // 0-based
            const auto num_bytes_needed =
                (element_locations_[element_loc_idx_].first) + 1;

            if (num_bytes_received_ >= num_bytes_needed) {
                // we can tell the tree builder to add the element to
                // the dom
                const auto elemInstNum =
                    (element_locations_[element_loc_idx_].second);

                num_bytes_parsed_ = num_bytes_needed;

                ++element_loc_idx_;

                _add_element_to_doc(elemInstNum);

            } else {
                // not enough byte to get the next element
                // yet. nothing to do here
                num_bytes_parsed_ = num_bytes_received_;
            }

            vlogself(2) << "new num_bytes_parsed_= " << num_bytes_parsed_;
        }
    }

    // done with the parsing loop
    
    if ((num_bytes_parsed_ == num_bytes_received_) && done_received_) {
        // we have reached the end, i.e., done parsing. we should not
        // be blocked by script right now
        CHECK(!hasParserBlockingScript());

        attemptToEnd();

        // proly here is where we fire the DOMContentLoaded event
    }

    if (hasParserBlockingScript()) {
        _do_preload_scanning();
    }

    vlogself(2) << "done";
}

void
HTMLDocumentParser::set_parser_blocking_script(HTMLScriptElement* elem)
{
    CHECK(!parser_blocking_script_);
    parser_blocking_script_ = elem;
    vlogself(2) << "elem:" << elem->instNum() << " now blocks parser";

    CHECK(parser_blocking_script_->is_parser_blocking());

    if (parser_blocking_script_->exec_immediately()) {
        // this means absolutely execute it now, irrespective of any
        // blocking css stylesheets

        vlogself(2) << "go execute it immediately!";

        // it should not refer to any resource, i.e., it should be
        // inline script
        CHECK_EQ(parser_blocking_script_->resInstNum(), 0);
        executeParsingBlockingScript();
    } else {
        if (isPendingScriptReady(parser_blocking_script_)) {
            vlogself(2) << "can go execute now because it's ready";
            executeParsingBlockingScript();
        }
    }
}

void
HTMLDocumentParser::_execute_script(uint32_t scope_id)
{
    // for now we don't support script recursion
    CHECK(!is_executing_script_);
    is_executing_script_ = true;

    webengine_->execute_scope(scope_id);

    is_executing_script_ = false;
}

void
HTMLDocumentParser::attemptToRunDeferredScriptsAndEnd()
{
    vlogself(2) << "begin";
    if (!executeScriptsWaitingForParsing()) {
        CHECK(0) << "todo";
        return;
    }
    end();
    vlogself(2) << "done";
}

void
HTMLDocumentParser::end()
{
    vlogself(2) << "begin";
    document_->finishedParsing();
    vlogself(2) << "done";
}

bool
HTMLDocumentParser::executeScriptsWaitingForParsing()
{
    vlogself(2) << "begin, " << m_scriptsToExecuteAfterParsing.size();

    CHECK(m_scriptsToExecuteAfterParsing.empty()) << "TODO";

    while (!m_scriptsToExecuteAfterParsing.empty()) {
        CHECK(!isExecutingScript());
        CHECK(!hasParserBlockingScript());

        HTMLScriptElement* script_elem = m_scriptsToExecuteAfterParsing.front();
        m_scriptsToExecuteAfterParsing.pop();

        /* assert that the script element is external, and for now
         * assert it is ready, i.e., the resource has downloaded */
        CHECK_GT(script_elem->resInstNum(), 0);
        CHECK(isPendingScriptReady(script_elem));

        vlogself(2) << "executing script elem:" << script_elem->instNum();
        _execute_script(script_elem->run_scope_id());
    }
    vlogself(2) << "done, returning true";
    return true;
}

void
HTMLDocumentParser::prepareToStopParsing()
{
    vlogself(2) << "begin";
    attemptToRunDeferredScriptsAndEnd();
    vlogself(2) << "done";
}

bool
HTMLDocumentParser::isWaitingForScripts() const
{
    return hasParserBlockingScript();
}

bool
HTMLDocumentParser::isExecutingScript() const
{
    return is_executing_script_;
}

bool
HTMLDocumentParser::shouldDelayEnd() const
{
    auto shoulddelay = false;
    if (inPumpSession()) {
        vlogself(2) << "currently in a parsing session";
        shoulddelay = true;
    } else if (isWaitingForScripts()) {
        vlogself(2) << "currently waiting for scripts";
        shoulddelay = true;
    } else if (isExecutingScript()) {
        vlogself(2) << "currently executing a script";
        shoulddelay = true;
    } else if (num_bytes_parsed_ < num_bytes_received_) {
        vlogself(2) << "still more bytes to parse";
        shoulddelay = true;
    }

    // NOT checking for done_received_

    return shoulddelay;
}

bool
HTMLDocumentParser::isPendingScriptReady(const HTMLScriptElement* script_elem)
{
    if (!document_->isScriptExecutionReady()) {
        return false;
    }

    // document allowing script execution, but is the script itself
    // ready?

    const auto resInstNum = script_elem->resInstNum();
    if (resInstNum) {
        shared_ptr<Resource> resource =
            resource_fetcher_->getResource(resInstNum);
        CHECK_NOTNULL(resource.get());

        // todo: what to do when the script finished BUT failed to
        // load?
        if (!resource->isFinished()) {
            vlogself(2) << "script elem:" << script_elem->instNum()
                        << " is NOT ready";
            return false;
        }
    }

    vlogself(2) << "script elem:" << script_elem->instNum() << " IS ready";
    return true;
}

void
HTMLDocumentParser::_add_element_to_doc(const uint32_t& elemInstNum)
{
    document_->add_elem(elemInstNum);
}

void
HTMLDocumentParser::_do_preload_scanning()
{
    vlogself(2) << "begin, num_preload_scanned_bytes_= "
                << num_preload_scanned_bytes_ << " "
                << "num_bytes_parsed_= "
                << num_bytes_parsed_;

    // check to make sure these variables didn't get changed (by
    // accident)
    const auto saved_element_loc_idx_ = element_loc_idx_;
    const auto saved_num_bytes_parsed_ = num_bytes_parsed_;
    const auto saved_num_bytes_received_ = num_bytes_received_;

    SCOPE_EXIT {
        CHECK_EQ(element_loc_idx_, saved_element_loc_idx_);
        CHECK_EQ(num_bytes_parsed_, saved_num_bytes_parsed_);
        CHECK_EQ(num_bytes_received_, saved_num_bytes_received_);
    };

    CHECK(hasParserBlockingScript());

    /* if the parsing has caught up/gotten past the preload scanning
     * progress, then we reset the preload scanner byte
     * offset. otherwise continue where we left off last time
     */

    if (num_preload_scanned_bytes_ < num_bytes_parsed_) {
        num_preload_scanned_bytes_ = num_bytes_parsed_;
    } else {
        // nothing here --- continue using the current
        // num_preload_scanned_bytes_
    }

    vlogself(2) << "new num_preload_scanned_bytes_= " << num_preload_scanned_bytes_;

    if (num_preload_scanned_bytes_ == num_bytes_received_) {
        vlogself(2) << "there's no bytes to scan now";
        return;
    }
    CHECK_LT(num_preload_scanned_bytes_, num_bytes_received_);

    if (no_more_preload_scanning_necessary_) {
        vlogself(2) << "will not find any new elements";
        return;
    }

    vlogself(2) << "find which idx in element_locations_ to start scanning";

    auto idx = _find_element_idx_to_begin_preload_scanning();
    if (idx < 0) {
        return;
    }

    vlogself(2) << "start trying to preload scanning, idx= " << idx;

    // these are to be preloaded
    vector<uint32_t> resInstNums;

    // this loop will be similar to the pump_parser() loop, except we
    // are using the num_preload_scanned_bytes_ instead of
    // num_bytes_parsed_, idx instead of element_loc_idx_, and we
    // loads the resource but not add the element
    while (num_preload_scanned_bytes_ < num_bytes_received_) {
        if (idx == element_locations_.size()) {
            // we are now PAST the end of element_locations_ array, so
            // we can stop doing preload scanning
            no_more_preload_scanning_necessary_ = true;
            num_preload_scanned_bytes_ = num_bytes_received_;
            break;
        } else {
            // REMEMBER that the location is byte offset, i.e.,
            // 0-based
            const auto num_bytes_needed =
                (element_locations_[idx].first) + 1;

            vlogself(2) << "element idx= " << idx
                        << " bytes needed= " << num_bytes_needed
                        << " bytes scanned= " << num_preload_scanned_bytes_
                        << " bytes avail= " << num_bytes_received_;

            if (num_bytes_received_ >= num_bytes_needed) {
                // we can preload the resource if the element has an
                // initial resource reference
                num_preload_scanned_bytes_ = num_bytes_needed;

                vlogself(2) << "  YES! scan element at idx (NOT byte-offset)= " << idx;

                const auto elemInstNum = element_locations_[idx].second;
                vlogself(2) << "get element initial_resInstNum";
                const auto resInstNum =
                    page_model_->get_element_initial_resInstNum(elemInstNum);
                if (resInstNum > 0) {
                    vlogself(2) << "it has initial res:"
                                << resInstNum;
                    resInstNums.push_back(resInstNum);
                }

                ++idx;
            } else {
                // not enough byte to get the next element
                // yet. nothing to do here
                vlogself(2) << "  not enough";
                num_preload_scanned_bytes_ = num_bytes_received_;
            }

            vlogself(2) << "new num_preload_scanned_bytes_= "
                        << num_preload_scanned_bytes_;
        }
    }

    if (!resInstNums.empty()) {
        resource_fetcher_->preload(resInstNums);
    }

    vlogself(2) << "done";
}

int
HTMLDocumentParser::_find_element_idx_to_begin_preload_scanning()
{
    CHECK_LT(num_preload_scanned_bytes_, num_bytes_received_);
    CHECK(!no_more_preload_scanning_necessary_);

    int idx = -1;
    for (auto i = 0; i < element_locations_.size(); ++i) {
        // keep in mind that loc is 0-based byte-offset
        const auto num_bytes_needed = (element_locations_[i].first) + 1;

        // we have received bytes past this element, but have we
        // already parsed/preload-scanned it yet?
        if (num_preload_scanned_bytes_ >= num_bytes_needed) {
            // we have already scanned it, so keep looking
            if (i == element_locations_.size()) {
                // there is no more elements to scan, so take a note
                no_more_preload_scanning_necessary_ = true;
            }
        } else {
            // we have NOT scanned it yet, so we have found where
            // to start
            idx = i;
            break;
        }
    }

    return idx;
}

} // end namespace blink
