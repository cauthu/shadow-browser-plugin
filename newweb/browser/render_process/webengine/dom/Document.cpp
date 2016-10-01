
#include "../../../../utility/easylogging++.h"

#include "Document.hpp"
#include "../webengine.hpp"



#define _LOG_PREFIX(inst) << "doc= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



namespace blink {


Document::Document(const uint32_t& instNum,
                   Webengine* webengine,
                   const PageModel* page_model,
                   ResourceFetcher* resource_fetcher)
    : Node(instNum)
    , webengine_(webengine)
    , page_model_(page_model)
    , resource_fetcher_(resource_fetcher)
    , state_(DocumentState::INITIAL)
{
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
Document::addElement(const uint32_t& elemInstNum)
{
    CHECK(0) << "todo";
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
