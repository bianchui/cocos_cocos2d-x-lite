// [BC]Uncaught Promise
namespace {
    
inline void v8UnusedBool(bool) {
}

template <typename T>
inline v8::Local<T> v8ToLocal(const v8::MaybeLocal<T>& v) {
    v8::Local<T> ret;
    v8UnusedBool(v.ToLocal(&ret));
    return ret;
}

const char* str_or_null(const char* str) {
    return str ? str : "null";
}

v8::Local<v8::String> NewOneByteString(v8::Isolate* isolate, const char* str) {
    return v8ToLocal(v8::String::NewFromOneByte(isolate, reinterpret_cast<const uint8_t*>(str), v8::NewStringType::kNormal));
}

std::string v8ExceptionDetail(v8::Isolate* isolate, v8::Local<v8::Message> message, v8::Local<v8::Value> er) {
    v8::HandleScope handleScope(isolate);
    v8::TryCatch fatal_try_catch(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    fatal_try_catch.SetVerbose(false);

    std::string output;

    v8::Local<v8::Value> trace_value;
    std::string name;

    if (er->IsUndefined() || er->IsNull()) {
        // did noting
    } else if (er->IsObject()) {
        v8::Local<v8::Object> err_obj = v8ToLocal(er->ToObject(ctx));
        v8::Local<v8::Value> obj_name;
        obj_name = v8ToLocal(err_obj->Get(ctx, NewOneByteString(isolate, "name")));
        if (!obj_name.IsEmpty() && !obj_name->IsUndefined()) {
            v8::String::Utf8Value name_value(isolate, obj_name);
            name = std::string(*name_value, name_value.length());
        }
        trace_value = v8ToLocal(err_obj->Get(ctx, NewOneByteString(isolate, "stack")));
    }

    v8::String::Utf8Value trace(isolate, trace_value);

    v8::Local<v8::Value> obj_msg;

    if (er->IsObject()) {
        v8::Local<v8::Object> err_obj = er.As<v8::Object>();
        obj_msg = v8ToLocal(err_obj->Get(ctx, NewOneByteString(isolate, "message")));
    }

    if (obj_msg.IsEmpty() || obj_msg->IsUndefined() || name.empty()) {
        // Not an error object. Just print as-is.
        v8::String::Utf8Value message_string(isolate, er);
        output.append(*message_string ? *message_string : "<toString() threw exception>");

    } else {
        v8::String::Utf8Value message_string(isolate, obj_msg);
        output.append(name);
        output.append(": ");
        output.append(str_or_null(*message_string));
    }
    output.append("\n");

    int line = message->GetLineNumber(ctx).FromMaybe(-1);
    int column = message->GetStartColumn(ctx).FromMaybe(-1) + 1;

    char buf[128];
    v8::String::Utf8Value file_name(isolate, message->GetScriptResourceName());
    output.append(str_or_null(*file_name));
    output.append(buf, sprintf(buf, ":%d:%d\n", line, column));

    v8::Local<v8::StackTrace> stack = message->GetStackTrace();
    if (!stack.IsEmpty()) {
        const int count = stack->GetFrameCount();
        for (int i = 0; i < count; ++i) {
            v8::Local<v8::StackFrame> frame = stack->GetFrame(isolate, i);
            v8::Local<v8::String> funName = frame->GetFunctionName();
            v8::String::Utf8Value fun(isolate, funName);
            v8::String::Utf8Value url(isolate, frame->GetScriptNameOrSourceURL());
            output.append("    at ");
            if (!funName.IsEmpty()) {
                output.append(str_or_null(*fun));
                output.append(" (");
            }
            output.append(str_or_null(*url));
            output.append(buf, sprintf(buf, ":%d:%d", frame->GetLineNumber(), frame->GetColumn()));
            if (!funName.IsEmpty()) {
                output.append(")");
            }
            output.append("\n");
        }
    }

    return output;
}

}

bool se::ScriptEngine::promiseReject(const v8::PromiseRejectMessage& msg) {
    v8::Isolate *isolate = _isolate;
    auto event = msg.GetEvent();
    if (event != v8::kPromiseRejectWithNoHandler && event != v8::kPromiseHandlerAddedAfterReject) {
        return false;
    }
    
    v8::Local<v8::Promise> promise = msg.GetPromise();
    if (promise.IsEmpty()) {
        return false;
    }
    
    if (event == v8::kPromiseRejectWithNoHandler) {
        const int hash = promise->GetIdentityHash();
        PendingUncaughtPromise* pending = new PendingUncaughtPromise;
        v8::Local<v8::Value> value = msg.GetValue();
        pending->promise.Reset(isolate, promise);
        if (!value.IsEmpty()) {
            pending->value.Reset(isolate, value);
            pending->message.Reset(isolate, v8::Exception::CreateMessage(isolate, value));
        }
        pending->hash = hash;
        _pendingUncaughtPromise.push_back(std::unique_ptr<PendingUncaughtPromise>(pending));
    } else if (event == v8::kPromiseHandlerAddedAfterReject) {
        const int hash = promise->GetIdentityHash();
        for (auto iter = _pendingUncaughtPromise.begin(), end = _pendingUncaughtPromise.end(); iter != end; ++iter) {
            if ((*iter)->hash == hash && (*iter)->promise == promise) {
                _pendingUncaughtPromise.erase(iter);
                break;
            }
        }
    }
    return true;
}

void se::ScriptEngine::reportUncaughtPromise(const PendingUncaughtPromise& pending) {
    v8::Isolate *isolate = _isolate;
    v8::HandleScope scope(isolate);
    v8::Local<v8::Promise> promise = pending.promise.Get(isolate);
    v8::Local<v8::Value> value = pending.value.Get(isolate);
    v8::Local<v8::Message> message = pending.message.Get(isolate);
    
    std::string str = v8ExceptionDetail(isolate, message, value);
    this->callExceptionCallback("", "unhandledRejectedPromise", str.c_str());
}
