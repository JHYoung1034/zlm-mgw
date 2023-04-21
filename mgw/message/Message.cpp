#include "Message.h"
#include "mgw.pb.h"
#include "google/protobuf/util/json_util.h"

#include "Util/logger.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

////////////////////////////////////////////////////////////////////////
ProtoBufDec::ProtoBufDec(const toolkit::BufferLikeString &buf) {
    _msg = make_shared<mgw::MgwMsg>();
    _msg->ParseFromArray(buf.data(), buf.size());
}

string ProtoBufDec::load() {
    string name;
    if (!_msg.operator bool()) {
        return name;
    }

    if (_msg->Message_case() > 0) {
        name = _msg->GetDescriptor()->FindFieldByNumber(_msg->Message_case())->name();
    }
    return name;
}

string ProtoBufDec::toString() const {
    return _msg->DebugString();
}

////////////////////////////////////////////////////////////////////////
ProtoBufEnc::ProtoBufEnc(const MsgPtr &msg) : _msg(msg) {

}

BufferLikeString::Ptr ProtoBufEnc::serialize() {
    BufferLikeString::Ptr result = nullptr;

    if (_msg->ByteSizeLong() <= 1)
        return result;

    string data;
    if (!_msg->SerializeToString(&data)) {
        goto end;
    }

    result = make_shared<BufferLikeString>(data);

end:
    return result;
}

}