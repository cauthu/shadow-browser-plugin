
#include <unistd.h>
#include <string>
#include <iostream>

#include <angelscript.h>
#include <angelscript/add_on/scriptstdstring/scriptstdstring.h>

#include "../../../utility/easylogging++.h"

#include "webengine.hpp"


using std::string;
using std::cout;
using std::endl;


static asIScriptEngine* s_as_script_engine = nullptr;


static int
MakeRequestID() {
    static int next_request_id = 0;
    return next_request_id++;
}


namespace blink
{

static void
s_as_MessageCallback(const asSMessageInfo *msg, void *param)
{
	const char *type = "ERR ";
	if( msg->type == asMSGTYPE_WARNING ) 
		type = "WARN";
	else if( msg->type == asMSGTYPE_INFORMATION ) 
		type = "INFO";

	LOG(INFO) << msg->section << " (" << msg->row << ":" << msg->col
              << "): " << type << " : " << msg->message;
}

/* sleep milliseconds */
static void
s_as_msleep(int msec)
{
    usleep(msec * 1000);
}

static void
s_as_addElement(uint32_t instNum)
{
    CHECK(0) << "todo";
}

static void
s_as_print(const string& s)
{
    LOG(INFO) << "script is printing [" << s << "]";
}

double mymulti(int i, double j)
{
  return i * j;
}

double myadd(int i, double j)
{
  return i + j;
}

Webengine::Webengine(IOServiceIPCClient* ioservice_ipcclient)
    : ioservice_ipcclient_(ioservice_ipcclient)
{
    static bool initialized = false;

    // there should be ONLY one webengine per process
    CHECK(!initialized);

    CHECK_NOTNULL(ioservice_ipcclient_);

    ioservice_ipcclient_->set_resource_msg_handler(this);

    _init_angelscript_engine();

    initialized = true;
}

void
Webengine::_init_angelscript_engine()
{
    CHECK_EQ(s_as_script_engine, nullptr);
    s_as_script_engine = asCreateScriptEngine();
    CHECK_NOTNULL(s_as_script_engine);

    LOG(INFO) << "angelscript engine= " << s_as_script_engine;

    asIScriptEngine* engine = s_as_script_engine;

    engine->SetMessageCallback(
        asFUNCTION(s_as_MessageCallback), 0, asCALL_CDECL);

    RegisterStdString(engine);

    auto rv = 0;

#define REGISTER_GLOBAL_AS_FUNC(signature_str, our_impl_func)           \
    do {                                                                \
        rv = engine->RegisterGlobalFunction(                            \
            signature_str, asFUNCTION(our_impl_func), asCALL_CDECL);    \
        CHECK_GE(rv, 0);                                                \
    } while (0)

    REGISTER_GLOBAL_AS_FUNC(
        "void msleep(int msec)", s_as_msleep);

    REGISTER_GLOBAL_AS_FUNC(
        "void print(const string& in)", s_as_print);

    REGISTER_GLOBAL_AS_FUNC(
        "double func1(int i, double d)", myadd);
    REGISTER_GLOBAL_AS_FUNC(
        "double func2(int i, double d)", mymulti);


    asIScriptModule *mod = engine->GetModule(0, asGM_ALWAYS_CREATE);
    CHECK_NOTNULL(mod);

    static const char script[] = "double calc(int a, double b) {"
                                 "print(\"i am about to sleep!!\");"
                                 "msleep(10000);"
                                 "return func1(a, b);"
                                 "}";

    auto r = mod->AddScriptSection("script", &script[0]);
    CHECK_GE(r, 0);

    r = mod->Build();
    CHECK_GE(r, 0);

    // Create a context that will execute the script.
    asIScriptContext *ctx = engine->CreateContext();
    CHECK_NOTNULL(ctx);

    asIScriptFunction *func = engine->GetModule(0)->GetFunctionByDecl("double calc(int, double)");
    CHECK_NOTNULL(func) << "execution scope must ";

    r = ctx->Prepare(func);
    CHECK_GE(r, 0);

	// Now we need to pass the parameters to the script function. 
    // ctx->SetArgDWord(0, 4);
    // ctx->SetArgDouble(1, 5.5);

    // r = ctx->Execute();
    // if( r != asEXECUTION_FINISHED ) {
    // // The execution didn't finish as we had planned. Determine why.
    // if( r == asEXECUTION_ABORTED )
    // 	cout << "The script was aborted before it could finish. Probably it timed out." << endl;
    // else if( r == asEXECUTION_EXCEPTION )
    // {
    // 	cout << "The script ended with an exception." << endl;

    // 	// Write some information about the script exception
    // 	asIScriptFunction *func = ctx->GetExceptionFunction();
    // 	cout << "func: " << func->GetDeclaration() << endl;
    // 	cout << "modl: " << func->GetModuleName() << endl;
    // 	cout << "sect: " << func->GetScriptSectionName() << endl;
    // 	cout << "line: " << ctx->GetExceptionLineNumber() << endl;
    // 	cout << "desc: " << ctx->GetExceptionString() << endl;
    // }
    // else
    // 	cout << "The script ended for some unforeseen reason (" << r << ")." << endl;

    // }

    // // Retrieve the return value from the context
    // double returnValue = ctx->GetReturnDouble();
    // LOG(INFO) << "return value: " << returnValue;
}

void
Webengine::loadPage(const char* model_fpath)
{
    const auto req_id = MakeRequestID();
    ioservice_ipcclient_->request_resource(
        3124, "cnn.com", 80, 123, 234, 63000 + (rand() % 1000));
}

void
Webengine::handle_ReceivedResponse(const int& req_id)
{
}

void
Webengine::handle_DataReceived(const int& req_id, const size_t& length)
{

}

void
Webengine::handle_RequestComplete(const int& req_id, const bool success)
{
    CHECK(0);
}

} // end namespace blink
