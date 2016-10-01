
#include <vector>

#include "HTMLDocumentParser.hpp"
#include "../HTMLScriptElement.hpp"
#include "../../webengine.hpp"

#include "../../../../../utility/easylogging++.h"
#include "../../../../../utility/folly/ScopeGuard.h"


using std::vector;


#define _LOG_PREFIX(inst) << "htmlparser= " << (inst)->objId() << ": "

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
{
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
}

bool
HTMLDocumentParser::hasParserBlockingScript() const
{
    return (parser_blocking_script_ != nullptr);
}

void
HTMLDocumentParser::pump_parser()
{
    vlogself(2) << "begin, parsed= " << num_bytes_parsed_
                << " received= " << num_bytes_received_;

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

        CHECK(0) << "todo";

        // proly here is where we fire the DOMContentLoaded event
    }

    if (hasParserBlockingScript()) {
        _do_preload_scanning();
    }

    vlogself(2) << "done";
}

void
HTMLDocumentParser::_add_element_to_doc(const uint32_t& elemInstNum)
{
    PageModel::ElementInfo elem_info;

    vlogself(2) << "begin, elemInstNum= " << elemInstNum;

    const auto rv = page_model_->get_element_info(elemInstNum, elem_info);
    CHECK(rv);

    std::shared_ptr<Element> element;

    if (elem_info.tag == "script") {
        element.reset(
            new HTMLScriptElement(elemInstNum, document_,
                                  elem_info.is_parser_blocking,
                                  elem_info.exec_immediately,
                                  elem_info.exec_async,
                                  elem_info.run_scope_id),
            [](HTMLScriptElement* e) { e->destroy(); }
            );
        HTMLScriptElement* script_elem = (HTMLScriptElement*)element.get();
        if (script_elem->is_parser_blocking()) {
            CHECK(!parser_blocking_script_);
            parser_blocking_script_ = script_elem;
        }
        if (script_elem->exec_immediately()) {
            CHECK_EQ(script_elem->resInstNum(), 0);
            vlogself(2) << "go execute!";
            webengine_->execute_scope(script_elem->run_scope_id());
        }
    }

    vlogself(2) << "done";
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
                    vlogself(2) << "it has initial_resInstNum= "
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
