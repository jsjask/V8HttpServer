// Copyright (c) 2015 Cesanta Software Limited
// All rights reserved

#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/libplatform/libplatform.h"
#include "include/v8.h"
using std::string;
using namespace v8;
static const char* s_http_port = "80";
static struct mg_serve_http_opts s_http_server_opts;
static v8::Isolate* isolate;
static v8::Isolate::CreateParams create_params;
#if 0
// Reads a file into a v8 string.
v8::MaybeLocal<v8::String> ReadFile(v8::Isolate* isolate, const string& name) {
    FILE* file = fopen(name.c_str(), "rb");
    if (file == NULL) return v8::MaybeLocal<v8::String>();

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    std::unique_ptr<char> chars(new char[size + 1]);
    chars.get()[size] = '\0';
    for (size_t i = 0; i < size;) {
        i += fread(&chars.get()[i], 1, size - i, file);
        if (ferror(file)) {
            fclose(file);
            return v8::MaybeLocal<v8::String>();
        }
    }
    fclose(file);
    v8::MaybeLocal<v8::String> result = v8::String::NewFromUtf8(
        isolate, chars.get(), v8::NewStringType::kNormal, static_cast<int>(size));
    return result;
}
#else
v8::MaybeLocal<v8::String> ReadFile(v8::Isolate* isolate, const string& name);
#endif

class HttpRequest {
public:
    ~HttpRequest() { }
    const string& Path() { return ""; };
    const string& Referrer() { return ""; };
    const string& Host() { return ""; };
    const string& UserAgent() { return ""; };
};
v8::Local<v8::Context> context;
static Global<ObjectTemplate> request_template_;
/**
 * Utility function that extracts the C++ http request object from a
 * wrapper object.
 */
HttpRequest* UnwrapRequest(Local<Object> obj) {
    Local<External> field = Local<External>::Cast(obj->GetInternalField(0));
    void* ptr = field->Value();
    return static_cast<HttpRequest*>(ptr);
}


void GetPath(Local<String> name,
    const PropertyCallbackInfo<Value>& info) {
    // Extract the C++ request object from the JavaScript wrapper.
    HttpRequest* request = UnwrapRequest(info.Holder());

    // Fetch the path.
    const string& path = request->Path();

    // Wrap the result in a JavaScript string and return it.
    info.GetReturnValue().Set(
        String::NewFromUtf8(info.GetIsolate(), path.c_str(),
            NewStringType::kNormal,
            static_cast<int>(path.length())).ToLocalChecked());
}


void GetReferrer(
    Local<String> name,
    const PropertyCallbackInfo<Value>& info) {
    HttpRequest* request = UnwrapRequest(info.Holder());
    const string& path = request->Referrer();
    info.GetReturnValue().Set(
        String::NewFromUtf8(info.GetIsolate(), path.c_str(),
            NewStringType::kNormal,
            static_cast<int>(path.length())).ToLocalChecked());
}


void GetHost(Local<String> name,
    const PropertyCallbackInfo<Value>& info) {
    HttpRequest* request = UnwrapRequest(info.Holder());
    const string& path = request->Host();
    info.GetReturnValue().Set(
        String::NewFromUtf8(info.GetIsolate(), path.c_str(),
            NewStringType::kNormal,
            static_cast<int>(path.length())).ToLocalChecked());
}


void GetUserAgent(
    Local<String> name,
    const PropertyCallbackInfo<Value>& info) {
    HttpRequest* request = UnwrapRequest(info.Holder());
    const string& path = request->UserAgent();
    info.GetReturnValue().Set(
        String::NewFromUtf8(info.GetIsolate(), path.c_str(),
            NewStringType::kNormal,
            static_cast<int>(path.length())).ToLocalChecked());
}
Local<ObjectTemplate> MakeRequestTemplate(
    Isolate* isolate) {
    EscapableHandleScope handle_scope(isolate);

    Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
    result->SetInternalFieldCount(1);

    // Add accessors for each of the fields of the request.
    result->SetAccessor(
        String::NewFromUtf8Literal(isolate, "path", NewStringType::kInternalized),
        GetPath);
    result->SetAccessor(String::NewFromUtf8Literal(isolate, "referrer",
        NewStringType::kInternalized),
        GetReferrer);
    result->SetAccessor(
        String::NewFromUtf8Literal(isolate, "host", NewStringType::kInternalized),
        GetHost);
    result->SetAccessor(String::NewFromUtf8Literal(isolate, "userAgent",
        NewStringType::kInternalized),
        GetUserAgent);

    // Again, return the result through the current handle scope.
    return handle_scope.Escape(result);
}

Local<Object> WrapRequest(HttpRequest* request) {
    // Local scope for temporary handles.
    EscapableHandleScope handle_scope(isolate);

    // Fetch the template for creating JavaScript http request wrappers.
    // It only has to be created once, which we do on demand.
    if (request_template_.IsEmpty()) {
        Local<ObjectTemplate> raw_template = MakeRequestTemplate(isolate);
        request_template_.Reset(isolate, raw_template);
    }
    Local<ObjectTemplate> templ =
        Local<ObjectTemplate>::New(isolate, request_template_);

    // Create an empty http request wrapper.
    Local<Object> result = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();

    // Wrap the raw C++ pointer in an External so it can be referenced
    // from within JavaScript.
    Local<External> request_ptr = External::New(isolate, request);

    // Store the request pointer in the JavaScript wrapper.
    result->SetInternalField(0, request_ptr);

    // Return the result through the current handle scope.  Since each
    // of these handles will go away when the handle scope is deleted
    // we need to call Close to let one, the result, escape into the
    // outer handle scope.
    return handle_scope.Escape(result);
}

Handle<Function> _handleRequestFunction;

static void addEventListener(const v8::FunctionCallbackInfo<v8::Value>& args) {
    if (args.Length() < 1) return;
    Isolate* isolate = args.GetIsolate();
    HandleScope scope(isolate);
    printf("addEventListener\n");
}

static void Response(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Handle<Object> _obj = Object::New(isolate);
    HandleScope scope(isolate);
    printf("Response\n");
    if (args.Length() >= 1)
    {
        Local<Value> arg = args[0];
        _obj->Set(context, 0, arg);
    }
    args.GetReturnValue().Set(_obj);

    //Local<Value> arg = args[0];
    //String::Utf8Value value(isolate, arg);
    //HttpRequestProcessor::Log(*value);
}

string ProcessRequest(HttpRequest* request)
{
    string resultStr;
    {
        v8::Isolate::Scope isolate_scope(isolate);

        // Create a stack-allocated handle scope.
        v8::HandleScope handle_scope(isolate);
        Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
        global->Set(isolate, "addEventListener", FunctionTemplate::New(isolate, addEventListener));
        global->Set(isolate, "Response", FunctionTemplate::New(isolate, Response));

        context = Context::New(isolate, NULL, global);

        // Enter the context for compiling and running the hello world script.
        v8::Context::Scope context_scope(context);
        Handle<Object> globalObj = context->Global();
        {
            // Create a string containing the JavaScript source code.
            string file = "handle_Request.js";
            if (file.empty()) {
                fprintf(stderr, "No script was specified.\n");
                return "No script was specified.";
            }

            v8::Local<v8::String> source;
            if (!ReadFile(isolate, file).ToLocal(&source)) {
                fprintf(stderr, "Error reading '%s'.\n", file.c_str());
                return "Error reading";
            }
            // Compile the source code.
            v8::Local<v8::Script> script =
                v8::Script::Compile(context, source).ToLocalChecked();
            script->Run(context);
            // Run the script to get the result.
            Local<Value> process_val;
            globalObj->Get(context, String::NewFromUtf8Literal(isolate, "handleRequest")).ToLocal(&process_val);
            _handleRequestFunction = process_val.As<Function>();
            if (_handleRequestFunction.IsEmpty() || !_handleRequestFunction->IsFunction())
                return "handleRequest not found!\n";
            HttpRequest request;
            // Wrap the C++ request object in a JavaScript wrapper
            Local<Object> request_obj = WrapRequest(&request);
            Handle<Function> func = Handle<Function>::Cast(_handleRequestFunction);
            Handle<Value> funArgs[1] = { request_obj };
            Handle<Object> globalObj = context->Global();
            Local<Value> result;
            func->Call(context, globalObj, 1, funArgs).ToLocal(&result);
            Handle<Object> resultObj = result.As<Object>();
            Handle<Value> arg = resultObj->Get(context, 0).ToLocalChecked();
            v8::String::Utf8Value utf8(isolate, arg);
            printf("%s\n", *utf8);
            resultStr = *utf8;
        }
    }
    return resultStr;
}

void ev_handler(struct mg_connection* nc, int ev, void* p) 
{
    if (ev == MG_EV_HTTP_REQUEST) 
    {
        string result = ProcessRequest(NULL);
        mg_send_head(nc, 200, result.size(),
            "Content-Type: text/plain\r\nConnection: close");
        mg_send(nc, result.c_str(), (int)(result.size()));
        nc->flags |= MG_F_SEND_AND_CLOSE;
        //mg_serve_http(nc, (struct http_message*)p, s_http_server_opts);
    }
}

#if 1
int main(int argc, char* argv[]) {
    struct mg_mgr mgr;
    struct mg_connection* nc;
    // Initialize V8.
    v8::V8::InitializeICUDefaultLocation(argv[0]);
    v8::V8::InitializeExternalStartupData(argv[0]);
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();
    // Create a new Isolate and make it the current one.
    create_params.array_buffer_allocator =
        v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    isolate = v8::Isolate::New(create_params);
    mg_mgr_init(&mgr, NULL);
    printf("Starting web server on port %s\n", s_http_port);
    nc = mg_bind(&mgr, s_http_port, ev_handler);
    if (nc == NULL) {
        printf("Failed to create listener\n");
        return 1;
    }

    // Set up HTTP server parameters
    mg_set_protocol_http_websocket(nc);
    s_http_server_opts.document_root = ".";  // Serve current directory
    s_http_server_opts.enable_directory_listing = "yes";

    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
    // Dispose the isolate and tear down V8.
    isolate->Dispose();
    delete create_params.array_buffer_allocator;
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    return 0;
}
#endif