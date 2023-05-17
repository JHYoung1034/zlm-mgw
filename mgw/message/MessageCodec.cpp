#include "MessageCodec.h"
#include "mgw.pb.h"

#include "Util/logger.h"
#include "Rtmp/utils.h"
#include "Network/Buffer.h"

using namespace std;
using namespace toolkit;


namespace mediakit {

MessageCodec::MessageCodec(Entity entity) : _entity(entity) {
    _session_id = entity == Entity_MgwServer ? createSessionId() : 0;
}

MessageCodec::~MessageCodec() {

}

uint32_t MessageCodec::createSessionId() {
    srand((unsigned int)time(NULL));
    return (uint32_t)rand();
}

void MessageCodec::onParseMessage(const char *data, size_t size) {

    if (size < sizeof(MsgHeader))
        return;

    //1.用MsgHeader指向数据，得到消息头
    auto header = (MsgHeader*)data;

    uint32_t magic          = ntohl(header->_magic);
    uint16_t payload_size   = ntohs(header->_payloadSize);
    uint32_t seq            = ntohl(header->_msgSeq);
    uint32_t ack_seq        = ntohl(header->_ackSeq);
    uint32_t session_id     = ntohl(header->_sessionId);

    //2.分析头信息是否正确
    if (magic != MAGIC_CODE) {
        PrintE("Error magic code:0x%x", magic);
        return;
    }

    if (session_id != _session_id) {
        if (_session_id == 0) {
            InfoL << "Session ID change: " << _session_id << "->" << session_id;
            _session_id = session_id;
        }
    }

    if ((_last_rcv_seq+1) != seq) {
        WarnL << "Error msgSeq:" << _last_rcv_seq << "->" << seq;
    }

    if (_peer_entity != header->_senderPid) {
        WarnL << "Peer Entity changed: " << _peer_entity << "->" << header->_senderPid;
        _peer_entity = (Entity)header->_senderPid;
    }

    _last_rcv_seq = seq;
    //3.创建一个 MessagePacket 消息包，把去掉MsgHeader的部分加载进MessagePacket的buffer内
    auto packet = MessagePacket::create();
    packet->_buffer.append(data + sizeof(MsgHeader), payload_size);
    packet->_sequence = seq;
    packet->_need_ack = (bool)header->_needAck;

    //4.调用onMgwMsg()交由继承者处理消息
    onMessage(packet);
}

void MessageCodec::StartClientSession(const function<void()> &cb, bool complex) {

}

void MessageCodec::getHeader(MsgHeader &header, uint32_t payload_size, bool need_ack, bool have_sub) {
    header._magic       = htonl(MAGIC_CODE);
    header._version     = MESSAGE_VER;
    header._headerSize  = sizeof(MsgHeader);
    header._payloadSize = htons(payload_size);
    header._msgSeq      = htonl(++_last_snd_seq);

    header._protoType   = _payload;
    header._senderPid   = _entity;
    header._receiverPid = _peer_entity;
    header._haveSubProto = have_sub;
    header._needAck     = need_ack;
    header._encryptType = _encrypt_type;
    header._ackSeq      = htonl(need_ack ? ++_last_rcv_seq : 0);
    header._sessionId   = htonl(_session_id);
}

void MessageCodec::sendSession(const string &sn, const string &dev_type,
                const string &version, const string &vendor,
                const string &access, uint32_t chn_nums,
                uint32_t max_bitrate, uint32_t max_bitrate4k) {

    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::SessionReq *session = msg->mutable_sessionreq();
    session->set_sn(sn);
    session->set_type(dev_type);
    session->set_version(version);
    session->set_vendor(vendor);
    session->set_outputcapacity(chn_nums);
    session->set_maxbitrate(max_bitrate);
    session->set_maxbitrate4k(max_bitrate4k);
    session->set_accesstoken(access);

    ProtoBufEnc enc(msg);
    sendRequest(enc);
}

void MessageCodec::sendStatusReq(uint32_t src_chn) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::SyncStatusReq *status_req = msg->mutable_syncreq();
    status_req->set_srcchn(src_chn);

    ProtoBufEnc enc(msg);
    sendRequest(enc);
}

void MessageCodec::sendPlayersAttr(bool enable, bool force_stop, const string &schema) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::SetPullAttr *attr = msg->mutable_setpullattr();
    int proto = 1;  // --> default rtmp
    if (schema == "srt") {
        proto = 2;
    } else if (schema == "udp_ts") {
        proto = 3;
    } else if (schema == "rtsp") {
        proto = 4;
    }

    attr->set_enable(enable);
    attr->set_forcestop(force_stop);
    attr->set_proto(proto);

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendStartProxyPush(uint32_t out_chn, const string &url, uint32_t src_chn) {
    if (url.empty()) {
        ErrorL << "Invalid url: " << url;
    }

    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::StartOutputStream *stream_req = msg->mutable_startoutput();
    stream_req->set_srcchn(src_chn);
    stream_req->set_outchn(out_chn);

    common::StreamAddress *addr = stream_req->mutable_address();
    if (url.substr(0, 7) == "rtmp://") {
        common::RTMPStreamAddress *rtmp = addr->mutable_rtmp();
        rtmp->set_uri(url);
    } else if (url.substr(0, 6) == "srt://") {
        common::SRTStreamAddress *srt = addr->mutable_srt();
        srt->set_simaddr(url);
    }

    ProtoBufEnc enc(msg);
    sendRequest(enc);
}

void MessageCodec::sendStopProxyPush(uint32_t out_chn, uint32_t src_chn) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::StopOutputStream *stop_req = msg->mutable_stopoutput();
    stop_req->set_srcchn(src_chn);
    stop_req->set_outchn(out_chn);

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendStartTunnelPush(uint32_t src_chn, const string &url) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::PushStreamReq *push_req = msg->mutable_pushstreamreq();
    push_req->set_chn(src_chn);
    push_req->set_proto(url);

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendStopTunnelPush(uint32_t src_chn, const string &url) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::StopPushingStream *req = msg->mutable_stoppushing();
    req->set_srcchn(src_chn);
    req->set_command(1);

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendComResponse(CmdType cmd, uint32_t src_chn,
                        uint32_t out_chn, int result, const string &des) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::CommonRsp *rsp = msg->mutable_response();
    rsp->set_command((int)cmd);
    rsp->set_srcchn(src_chn);
    rsp->set_outchn(out_chn);
    rsp->set_result(result);
    rsp->set_descrip(des);

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

//mgw-server  --> agg-server
void MessageCodec::sendSvrSession(uint64_t uptime, uint32_t interval, const string &host,
            uint16_t port, const string &path, const string &key, const string &version) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::ServerSessionReq *req = msg->mutable_sersessionreq();
    req->set_hbinterval(interval);
    req->set_serverport(port);
    req->set_serverhost(host);
    req->set_serverpath(path);
    req->set_secretkey(key);
    req->set_serverver(version);
    req->set_uptime(uptime);

    ProtoBufEnc enc(msg);
    sendRequest(enc);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
//发送消息到u727
void MessageCodec::sendDeviceOnline(const string &sn,
                const string &type,const string &ver,
                const string &ven, const string play_url) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::MgwDevOnlineNotify *notify = msg->mutable_devonline();
    u727::MgwDevInfo *info = notify->mutable_device();
    info->set_dev_sn(sn);
    info->set_type(type);
    info->set_version(ver);
    info->set_vendor(ven);
    info->set_stream_url(play_url);

    DebugL << "online:" << msg->DebugString();

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendDeviceOffline(const string &sn) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::MgwDevOfflineNotify *notify = msg->mutable_devoffline();
    notify->set_dev_sn(sn);

    DebugL << "offline:" << msg->DebugString();

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendStreamStatus(const string &stream_id, int status, int start_time, int err) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::StreamStatusNotify *notify = msg->mutable_streamstatus();
    notify->set_stream_id(stream_id);
    notify->set_status(status);
    notify->set_starttime(start_time);
    notify->set_lasterrcode(err);

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendStreamStarted(const string &sn, uint32_t channel, const string &url) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::DevStartStreamNotify *notify = msg->mutable_devstartstreamnotify();
    common::StreamAddress *addr = notify->mutable_dest_addr();

    notify->set_dev_sn(sn);
    notify->set_channel(channel);

    if (url.substr(0, 7) == "rtmp://") {
        common::RTMPStreamAddress *rtmp = addr->mutable_rtmp();
        rtmp->set_uri(url);
    } else if (url.substr(0, 6) == "srt://") {
        common::SRTStreamAddress *srt = addr->mutable_srt();
        srt->set_simaddr(url);
    }

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

void MessageCodec::sendStreamStoped(const string &sn, uint32_t channel, const string &url) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::DevStopStreamNotify *notify = msg->mutable_devstopstreamnotify();
    common::StreamAddress *addr = notify->mutable_dest_addr();

    notify->set_dev_sn(sn);
    notify->set_channel(channel);

    if (url.substr(0, 7) == "rtmp://") {
        common::RTMPStreamAddress *rtmp = addr->mutable_rtmp();
        rtmp->set_uri(url);
    } else if (url.substr(0, 6) == "srt://") {
        common::SRTStreamAddress *srt = addr->mutable_srt();
        srt->set_simaddr(url);
    }

    ProtoBufEnc enc(msg);
    sendMessage(enc, false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
//发送mgw消息
void MessageCodec::sendMessage(ProtoBufEnc &enc, bool need_ack) {
    //1.获取消息体
    BufferLikeString::Ptr payload = enc.serialize();
    if (!payload.operator bool()) {
        ErrorL << "Get protobuf message failed!";
        return;
    }

    //2.打包头格式
    MsgHeader header;
    getHeader(header, payload->size(), need_ack);
    ((BufferLikeString*)(payload.get()))->insert(0, (const char*)&header, sizeof(header));

    //3.调用onSendRawData()交给继承者发送消息实体
    onSendRawData(payload);
}
//一般发送请求消息都需要对方的回复，如果请求消息不需要回复，请使用sendMessage()发送消息
void MessageCodec::sendRequest(ProtoBufEnc &enc) {
    sendMessage(enc, true);
}
//一般发送回复消息不需要对方再回复了
void MessageCodec::sendResponse(ProtoBufEnc &enc) {
    sendMessage(enc, false);
}

}
