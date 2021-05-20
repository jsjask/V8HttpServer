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

class HttpRequest {
private:
    string header;
    string body;
public:
    ~HttpRequest() { }
    const string& GetHeader() { return header; };
    void SetHeader(string s) { header = s; };
    const string& GetBody() { return body; };
    void SetBody(string s) { body = s; };
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


void GetHeader(Local<String> name,
    const PropertyCallbackInfo<Value>& info) {
    // Extract the C++ request object from the JavaScript wrapper.
    HttpRequest* request = UnwrapRequest(info.Holder());

    // Fetch the path.
    const string& path = request->GetHeader();

    // Wrap the result in a JavaScript string and return it.
    info.GetReturnValue().Set(
        String::NewFromUtf8(info.GetIsolate(), path.c_str(),
            NewStringType::kNormal,
            static_cast<int>(path.length())).ToLocalChecked());
}


void GetBody(
    Local<String> name,
    const PropertyCallbackInfo<Value>& info) {
    HttpRequest* request = UnwrapRequest(info.Holder());
    const string& path = request->GetBody();
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
    result->SetAccessor(String::NewFromUtf8Literal(isolate, "header", 
        NewStringType::kInternalized),
        GetHeader);
    result->SetAccessor(String::NewFromUtf8Literal(isolate, "body",
        NewStringType::kInternalized),
        GetBody);
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
    HandleScope scope(isolate);
    printf("Response\n");
    static HttpRequest request;
    Local<Object> request_obj = WrapRequest(&request);
    if (args.Length() >= 2)
    {
        Local<Value> arg = args[0];
        request_obj->Set(context, 0, arg);
    }
    args.GetReturnValue().Set(request_obj);
}

static void mg_response(struct mg_connection* nc, string body)
{
    mg_send_head(nc, 200, body.size(),
        "Content-Type: text/plain\r\nConnection: close");
    mg_send(nc, body.c_str(), (int)(body.size()));
    nc->flags |= MG_F_SEND_AND_CLOSE;
}

void ProcessRequest(struct mg_connection* nc, HttpRequest* request)
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
            mg_http_send_error(nc, 400, "No script was specified.");
            return;
        }

        v8::Local<v8::String> source;
        if (!ReadFile(isolate, file).ToLocal(&source)) {
            string resultStr = "Error reading";
            resultStr.append(file.c_str());
            mg_http_send_error(nc, 400, resultStr.c_str());
            return;
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
        {
            mg_http_send_error(nc, 400, "handleRequest not found!");
            return;
        }
        // Wrap the C++ request object in a JavaScript wrapper
        Local<Object> request_obj = WrapRequest(request);
        Handle<Function> func = Handle<Function>::Cast(_handleRequestFunction);
        Handle<Value> funArgs[1] = { request_obj };
        Handle<Object> globalObj = context->Global();
        Local<Value> result;
        func->Call(context, globalObj, 1, funArgs).ToLocal(&result);
        Handle<Object> resultObj = result.As<Object>();
        Handle<Value> arg = resultObj->Get(context, 0).ToLocalChecked();
        v8::String::Utf8Value utf8(isolate, arg);
        printf("%s\n", *utf8);
        mg_response(nc, *utf8);
    }

}

void ev_handler(struct mg_connection* nc, int ev, void* p) 
{
    if (ev == MG_EV_HTTP_REQUEST) 
    {
        HttpRequest request;
        ProcessRequest(nc , &request);
    }
}

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