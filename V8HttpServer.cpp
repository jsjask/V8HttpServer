// Copyright (c) 2015 Cesanta Software Limited
// All rights reserved

#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/libplatform/libplatform.h"
#include "include/v8.h"
using std::string;
static const char* s_http_port = "80";
static struct mg_serve_http_opts s_http_server_opts;
static v8::Isolate* isolate;
static v8::Isolate::CreateParams create_params;
#if 1
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
#endif

v8::MaybeLocal<v8::String> ReadFile(v8::Isolate* isolate, const string& name);
string parseJs()
{
    string resultStr;
    {
        v8::Isolate::Scope isolate_scope(isolate);

        // Create a stack-allocated handle scope.
        v8::HandleScope handle_scope(isolate);

        // Create a new context.
        v8::Local<v8::Context> context = v8::Context::New(isolate);

        // Enter the context for compiling and running the hello world script.
        v8::Context::Scope context_scope(context);

        {
            // Create a string containing the JavaScript source code.
            string file = "hello_world.js";
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

            // Run the script to get the result.
            v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
            // Convert the result to an UTF8 string and print it.
            v8::String::Utf8Value utf8(isolate, result);
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
        string result = parseJs();
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