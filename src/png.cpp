#include <cstring>
#include <cstdlib>
#include "common.h"
#include "png_encoder.h"
#include "png.h"

using namespace v8;
using namespace node;

void
Png::Initialize(Handle<Object> target)
{
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);
    t->InstanceTemplate()->SetInternalFieldCount(1);
    NODE_SET_PROTOTYPE_METHOD(t, "encode", PngEncodeAsync);
    NODE_SET_PROTOTYPE_METHOD(t, "encodeSync", PngEncodeSync);
    target->Set(String::NewSymbol("Png"), t->GetFunction());
}

Png::Png(Buffer *ddata, int wwidth, int hheight, buffer_type bbuf_type) :
    data(ddata), width(wwidth), height(hheight), buf_type(bbuf_type) {}

Handle<Value>
Png::PngEncodeSync()
{
    HandleScope scope;

    try {
        PngEncoder p((unsigned char *)data->data(), width, height, buf_type);
        p.encode();
        return scope.Close(Encode((char *)p.get_png(), p.get_png_len(), BINARY));
    }
    catch (const char *err) {
        return VException(err);
    }
}

Handle<Value>
Png::New(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() < 3)
        return VException("At least three arguments required - data buffer, width, height, [and input buffer type]");
    if (!Buffer::HasInstance(args[0]))
        return VException("First argument must be Buffer.");
    if (!args[1]->IsInt32())
        return VException("Second argument must be integer width.");
    if (!args[2]->IsInt32())
        return VException("Third argument must be integer height.");

    buffer_type buf_type = BUF_RGB;
    if (args.Length() == 4) {
        if (!args[3]->IsString())
            return VException("Fourth argument must be 'rgb', 'bgr', 'rgba' or 'bgra'.");

        String::AsciiValue bts(args[3]->ToString());
        if (!(str_eq(*bts, "rgb") || str_eq(*bts, "bgr") ||
            str_eq(*bts, "rgba") || str_eq(*bts, "bgra")))
        {
            return VException("Fourth argument must be 'rgb', 'bgr', 'rgba' or 'bgra'.");
        }
        
        if (str_eq(*bts, "rgb"))
            buf_type = BUF_RGB;
        else if (str_eq(*bts, "bgr"))
            buf_type = BUF_BGR;
        else if (str_eq(*bts, "rgba"))
            buf_type = BUF_RGBA;
        else if (str_eq(*bts, "bgra"))
            buf_type = BUF_BGRA;
        else
            return VException("Fourth argument wasn't 'rgb', 'bgr', 'rgba' or 'bgra'.");
    }

    Buffer *data = ObjectWrap::Unwrap<Buffer>(args[0]->ToObject());
    int w = args[1]->Int32Value();
    int h = args[2]->Int32Value();

    if (w < 0)
        return VException("Width smaller than 0.");
    if (h < 0)
        return VException("Height smaller than 0.");

    Png *png = new Png(data, w, h, buf_type);
    png->Wrap(args.This());
    return args.This();
}

Handle<Value>
Png::PngEncodeSync(const Arguments &args)
{
    HandleScope scope;
    Png *png = ObjectWrap::Unwrap<Png>(args.This());
    return scope.Close(png->PngEncodeSync());
}

int
Png::EIO_PngEncode(eio_req *req)
{
    encode_request *enc_req = (encode_request *)req->data;
    Png *png = (Png *)enc_req->png_obj;

    try {
        PngEncoder p((unsigned char *)png->data->data(), png->width, png->height, png->buf_type);
        p.encode();
        enc_req->png_len = p.get_png_len();
        enc_req->png = (char *)malloc(sizeof(*enc_req->png)*enc_req->png_len);
        if (!enc_req->png) {
            enc_req->error = strdup("malloc in Png::EIO_PngEncode failed.");
            return 0;
        }
        else {
            memcpy(enc_req->png, p.get_png(), enc_req->png_len);
        }
    }
    catch (const char *err) {
        enc_req->error = strdup(err);
    }

    return 0;
}

int 
Png::EIO_PngEncodeAfter(eio_req *req)
{
    HandleScope scope;

    ev_unref(EV_DEFAULT_UC);
    encode_request *enc_req = (encode_request *)req->data;

    Handle<Value> argv[2];

    if (enc_req->error) {
        argv[0] = Undefined();
        argv[1] = ErrorException(enc_req->error);
    }
    else {
        argv[0] = Local<Value>::New(Encode(enc_req->png, enc_req->png_len, BINARY));
        argv[1] = Undefined();
    }

    TryCatch try_catch; // don't quite see the necessity of this

    enc_req->callback->Call(Context::GetCurrent()->Global(), 2, argv);

    if (try_catch.HasCaught())
        FatalException(try_catch);

    enc_req->callback.Dispose();
    free(enc_req->png);
    free(enc_req->error);

    ((Png *)enc_req->png_obj)->Unref();
    free(enc_req);

    return 0;
}

Handle<Value>
Png::PngEncodeAsync(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() != 1)
        return VException("One argument required - callback function.");

    if (!args[0]->IsFunction())
        return VException("First argument must be a function.");

    Local<Function> callback = Local<Function>::Cast(args[0]);
    Png *png = ObjectWrap::Unwrap<Png>(args.This());

    encode_request *enc_req = (encode_request *)malloc(sizeof(*enc_req));
    if (!enc_req)
        return VException("malloc in Png::PngEncodeAsync failed.");

    enc_req->callback = Persistent<Function>::New(callback);
    enc_req->png_obj = png;
    enc_req->png = NULL;
    enc_req->png_len = 0;
    enc_req->error = NULL;

    eio_custom(EIO_PngEncode, EIO_PRI_DEFAULT, EIO_PngEncodeAfter, enc_req);

    ev_ref(EV_DEFAULT_UC);
    png->Ref();

    return Undefined();
}

