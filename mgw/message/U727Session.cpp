#include "MessageSession.h"
#include "mgw.pb.h"
#include "Common/config.h"
#include "Util/onceToken.h"
#include "core/U727.h"

#include <chrono>

using namespace std;
using namespace toolkit;

namespace mediakit {

U727Session::U727Session(const Socket::Ptr &sock)
    : Session(sock), MessageCodec(Entity_MgwServer),
      _u727(make_shared<U727>()) {
    DebugP(this);
}

U727Session::~U727Session() {

    if (_event_handle_init) {
        NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastDeviceOnline);
        NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastPush);
        NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastStreamStatus);
    }

    uint64_t duration = _ticker.createdTime() / 1000;
    WarnP(this) << "耗时(s): " << duration;
}

void U727Session::onError(const SockException &err) {
    DebugP(this) << err.what();
}

void U727Session::onManager() {
    //超时没有收到消息，关闭它。一般正常情况下，设备会间隔10秒发送一个保活请求
    GET_CONFIG(uint32_t, cli_timeout, WsSrv::kKeepaliveSec);
    if (_ticker.elapsedTime() > cli_timeout * 1000) {
        shutdown(SockException(Err_timeout, "Recv data from device timeout"));
    }
}

void U727Session::onRecv(const Buffer::Ptr &buffer) {
    //复位保活计数时间
    _ticker.resetTime();
    onParseMessage(buffer->data(), buffer->size());
}

/// @brief Process u727 messages
/// @param msg_pkt 
void U727Session::onMessage(MessagePacket::Ptr msg_pkt) {
    //1.根据MessagePacket的负载，生成一个ProtoBufDec对象
    ProtoBufDec dec(msg_pkt->_buffer);
    //2.交给onProcessCmd处理
    onProcessCmd(dec);
}

/// @brief Send raw data to network
/// @param buffer 
void U727Session::onSendRawData(toolkit::Buffer::Ptr buffer) {
    _total_bytes += buffer->size();
    send(move(buffer));
}

void U727Session::setEventHandle() {
    weak_ptr<U727Session> weak_self = dynamic_pointer_cast<U727Session>(shared_from_this());
    //设备上/下线通知
    NoticeCenter::Instance().addListener(this, Broadcast::kBroadcastDeviceOnline, [weak_self](BroadcastDeviceOnlineArgs){
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        strong_self->getPoller()->async([weak_self, enable, config](){
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }

            if (enable) {
                strong_self->sendDeviceOnline(config.sn, config.type, config.version, config.vendor);
            } else {
                strong_self->sendDeviceOffline(config.sn);
            }
        }, false);
    });

    //设备开启/停止推流通知
    NoticeCenter::Instance().addListener(this, Broadcast::kBroadcastPush, [weak_self](BroadcastPushArgs){
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        strong_self->getPoller()->async([weak_self, enable, sn, channel, url](){
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }

            if (enable) {
                strong_self->sendStreamStarted(sn, channel, url);
            } else {
                strong_self->sendStreamStoped(sn, channel, url);
            }
        }, false);
    });

    //推流状态通知
    NoticeCenter::Instance().addListener(this, Broadcast::kBroadcastStreamStatus, [weak_self](BroadcastStreamStatusArgs){
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        strong_self->getPoller()->async([weak_self, stream_id, status, start_time, err_code](){
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }

            strong_self->sendStreamStatus(stream_id, status, start_time, err_code);
        }, false);
    });
}

/// @brief 
/// @param dec 
void U727Session::onProcessCmd(ProtoBufDec &dec) {
    typedef void (U727Session::*cmd_function)(ProtoBufDec &dec);
    static unordered_map<string, cmd_function> s_cmd_functions;
    static onceToken token([]() {
        s_cmd_functions.emplace("setBlackList", &U727Session::onMsg_setBlackList);
        s_cmd_functions.emplace("setSvcPortReq", &U727Session::onMsg_setSrvPort);
        s_cmd_functions.emplace("getSvcPortReq", &U727Session::onMsg_getSrvPort);
        s_cmd_functions.emplace("startStreamReq", &U727Session::onMsg_startStreamReq);
        s_cmd_functions.emplace("stopStream", &U727Session::onMsg_stopStream);
        s_cmd_functions.emplace("u727keepAlive", &U727Session::onMsg_u727KeepAlive);
        s_cmd_functions.emplace("queryOnlineDevReq", &U727Session::onMsg_queryOnlineDevReq);

    });

    if (!_event_handle_init) {
        setEventHandle();
        _event_handle_init = true;
    }

    string method = dec.load();
    auto it = s_cmd_functions.find(method);
    if (it == s_cmd_functions.end()) {
        //error message
        ErrorP(this) << "Not found the method:" << method;
        return;
    }
    auto func = it->second;
    (this->*func)(dec);
}

/////////////////////////////////////////////////////////////////////////////////////
//处理u727消息
void U727Session::onMsg_setBlackList(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_setBlackList:" << dec.toString();

    const u727::SetDevBlacklist &list = dec.messge()->setblacklist();
    for (auto i = 0; i < list.dev_sn_list_size(); i++) {
        _u727->setBlackDevice(list.dev_sn_list(i));
    }
}

void U727Session::onMsg_setSrvPort(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_setSrvPort:" << dec.toString();

    const u727::SetLocalSvcPortReq &req = dec.messge()->setsvcportreq();
    _u727->setRtmpPort((uint16_t)req.rtmp_port());
    _u727->setSrtPort((uint16_t)req.srt_port());
    _u727->setHttpPort((uint16_t)req.http_port());

    ////////////////////////////////////////////////////
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::SetLocalSvcPortReply *reply = msg->mutable_setsvcportreply();

    reply->set_result(0);
    reply->set_descrip("success");

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

void U727Session::onMsg_getSrvPort(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_getSrvPort:" << dec.toString();

    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::GetLocalSvcPortReply *reply = msg->mutable_getsvcportreply();

    reply->set_rtmp_port(_u727->getRtmpPort());
    reply->set_srt_port(_u727->getSrtPort());
    reply->set_http_port(_u727->getHttpPort());

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

void U727Session::startStream(const string &stream_id, uint32_t delay_ms, StreamType src_type,
            const string &src, StreamType dest_type, const string &dest) {

    MediaInfo info;

    Device::Ptr device = nullptr;
    if (src_type == StreamType_Dev && !src.empty()) {
        GET_CONFIG(string, tunnel_protocol, Mgw::kStreamTunnel);
        device = DeviceHelper::findDevice(src, false);
        if (!device) {
            WarnL << "Do not exist device: " << src;
            return;
        }

        string src_url = device->getPushaddr(0, tunnel_protocol);
        if (info.getUrl().empty()) {
            WarnL << "Can't parse this source url: " << src_url;
        }
        info.parse(src_url);
    }

    weak_ptr<U727Session> weak_self = dynamic_pointer_cast<U727Session>(shared_from_this());
    auto push_task = [weak_self, stream_id, dest, device/*, on_published, on_shutdown*/](const MediaSource::Ptr &src) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        //执行推流任务
        if (src) {
            // strong_self->_device_helper->addPusher(out_name, url, src, on_published, on_shutdown);
            auto pusher = device->pusher(stream_id);
            string url("dmsp://"); url.append(dest);

            if (pusher->status() != ChannelStatus_Idle) {
                //如果是tunnel pusher，不能手动中断去重推，否则会影响已存在的代理推流
                if (stream_id == TUNNEL_PUSHER) {
                    return;
                }

                WarnL << "Already exist, release the old and create a new: " << stream_id;
                pusher->restart(url, nullptr, nullptr, src);
            } else {
                pusher->start(url, nullptr, nullptr, 15, src);
            }

        } else {
            //过了一段时间还没有等到流，通知设备，推流失败
            DebugL << "没有找到需要的源";
            // on_published("Stream Not found.", ChannelStatus_Idle, ::time(NULL), -1);
        }
    };
    //异步查找流，找到了就执行推流任务
    MediaSource::findAsync(info, shared_from_this(), push_task);
}

static void parseStartStreamReq(const u727::StartStreamReq &req,
            StreamType &src_type, string &src, StreamType &dest_type, string &dest) {

    switch (req.src_case()) {
        case u727::StartStreamReq::kSrcSock: {
            src_type = StreamType_Sock;
            src = req.src_sock();
            break;
        }
        case u727::StartStreamReq::kSrcDevSn: {
            src_type = StreamType_Dev;
            src = req.src_dev_sn();
            break;
        }
        case u727::StartStreamReq::kSrcPullAddr: {
            const common::StreamAddress &addr = req.src_pull_addr();
            if (addr.has_rtmp()) {
                src = addr.rtmp().uri();
                src.append(addr.rtmp().code());
            } else {
                src = addr.srt().simaddr();
            }
            break;
        }
        case u727::StartStreamReq::kSrcPushAddr: {
            const common::StreamAddress &addr = req.src_push_addr();
            if (addr.has_rtmp()) {
                src = addr.rtmp().uri();
                src.append(addr.rtmp().code());
            } else {
                src = addr.srt().simaddr();
            }
            break;
        }
        case u727::StartStreamReq::kSrcFilePath: {
            src_type = StreamType_File;
            src = req.src_file_path();
            break;
        }
        default: break;
    }

    switch (req.dest_case()) {
        case u727::StartStreamReq::kDestSock: {
            dest_type = StreamType_Sock;
            dest = req.dest_sock();
            break;
        }
        case u727::StartStreamReq::kDestPullAddr: {
            const common::StreamAddress &addr = req.dest_pull_addr();
            if (addr.has_rtmp()) {
                dest = addr.rtmp().uri();
                dest.append(addr.rtmp().code());
            } else {
                dest = addr.srt().simaddr();
            }
            break;
        }
        case u727::StartStreamReq::kDestPushAddr: {
            const common::StreamAddress &addr = req.dest_push_addr();
            if (addr.has_rtmp()) {
                dest = addr.rtmp().uri();
                dest.append(addr.rtmp().code());
            } else {
                dest = addr.srt().simaddr();
            }
            break;
        }
        case u727::StartStreamReq::kDestFilePath: {
            dest_type = StreamType_File;
            dest = req.dest_file_path();
            break;
        }
        default: break;
    }
}

void U727Session::onMsg_startStreamReq(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_startStreamReq:" << dec.toString();

    int result = 0;
    string des("success"), src, dest;
    StreamType src_type = StreamType_None, dest_type = StreamType_None;
    const u727::StartStreamReq &req = dec.messge()->startstreamreq();

    parseStartStreamReq(req, src_type, src, dest_type, dest);

    //开始domain socket 推流到u727
    startStream(req.stream_id(), req.delay_ms(), src_type, src, dest_type, dest);

    ////////////////////////////////////////////////////
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::StartStreamReply *reply = msg->mutable_startstreamreply();
    reply->set_stream_id(req.stream_id());
    reply->set_result(result);
    reply->set_descrip(des);

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

void U727Session::onMsg_stopStream(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_stopStream:" << dec.toString();
    const u727::StopStreamReq &req = dec.messge()->stopstream();
    _u727->stopStream(req.stream_id());
}

void U727Session::onMsg_u727KeepAlive(ProtoBufDec &dec) {
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    msg->mutable_u727keepalive();
    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

void U727Session::onMsg_queryOnlineDevReq(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_queryOnlineDevReq:" << dec.toString();

    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::QueryOnlineDevReply *reply = msg->mutable_queryonlinedevreply();

    DeviceHelper::device_for_each([reply](Device::Ptr device){
        Device::DeviceConfig config = device->getConfig();
        u727::MgwDevInfo *info = reply->add_devices();
        info->set_dev_sn(config.sn);
        info->set_type(config.type);
        info->set_version(config.version);
        info->set_vendor(config.vendor);
    });

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

}
