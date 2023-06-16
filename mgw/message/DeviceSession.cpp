#include "MessageSession.h"
#include "mgw.pb.h"
#include "Common/config.h"
#include "Util/onceToken.h"
#include "core/U727.h"

#include <chrono>

using namespace std;
using namespace toolkit;
using namespace mediakit;

namespace MGW {

DeviceSession::DeviceSession(const Socket::Ptr &sock)
    : Session(sock), MessageCodec(Entity_MgwServer) {
    DebugP(this);
}

DeviceSession::~DeviceSession() {
    uint64_t duration = _ticker.createdTime() / 1000;
    WarnP(this) << "耗时(s): " << duration;

    //如果音视频流也不活跃，应该删除这个设备实例
    if (_device_helper) {
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastDeviceOnline,
                                false, _device_helper->device()->getConfig());

        _device_helper->device()->setAlive(true, false);
        if (!_device_helper->device()->streamAlive()) {
            _device_helper->releaseDevice();
        }
    }
}

void DeviceSession::onError(const SockException &err) {
    DebugP(this) << err.what();
}

void DeviceSession::onManager() {
    //超时没有收到设备的消息，关闭它。一般正常情况下，设备会间隔10秒发送一个状态同步请求
    GET_CONFIG(uint32_t, cli_timeout, WsSrv::kKeepaliveSec);
    if (_ticker.elapsedTime() > cli_timeout * 1000) {
        shutdown(SockException(Err_timeout, "Recv data from device timeout"));
    }
}

void DeviceSession::onRecv(const Buffer::Ptr &buffer) {
    //复位保活计数时间
    _ticker.resetTime();
    onParseMessage(buffer->data(), buffer->size());
}

/// @brief Process mgw messages
/// @param msg_pkt 
void DeviceSession::onMessage(MessagePacket::Ptr msg_pkt) {
    //1.根据MessagePacket的负载，生成一个ProtoBufDec对象
    ProtoBufDec dec(msg_pkt->_buffer);
    //2.交给onProcessCmd处理
    onProcessCmd(dec);
}

void DeviceSession::setEventHandle() {
    weak_ptr<DeviceSession> weak_self = dynamic_pointer_cast<DeviceSession>(shared_from_this());
    _device_helper->setOnNoReader([weak_self](const string &id){
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        //设备到mgw-server的流当前已经没有消费者了，通知设备停止传输这个流
        //切换回此设备会话线程再处理消息发送
        strong_self->getPoller()->async([weak_self, id](){
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            int chn = getSourceChn(id);
            strong_self->sendStopTunnelPush(chn);
        }, false);
    });

    _device_helper->setOnPlayersChange([weak_self](bool local, int players){
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        //TODO：后续需要实时同步到设备的时候，在这里发送消息给设备。现在暂时先不处理。
    });

    _device_helper->setOnNotFoundStream([weak_self](const string &url){
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        //mgw-server在收到play请求或者proxyPushing请求时，如果不存在该设备的流，
        //发送通知给设备，请求设备把流推送到mgw-server
        //切换回此设备会话线程再处理消息发送,使用最高优先级发送该消息，才能快速收到推流
        strong_self->getPoller()->async_first([weak_self, url](){
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            strong_self->sendStartTunnelPush(0, url);
        }, false);
    });

    //收到推拉流事件，由设备处理鉴权
    _device_helper->setOnAuthen([weak_self](const string &url)->bool {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        return strong_self->_device_helper->device()->availableAddr(url);
    });
}

/// @brief Send raw data to network
/// @param buffer 
void DeviceSession::onSendRawData(toolkit::Buffer::Ptr buffer) {
    _total_bytes += buffer->size();
    send(move(buffer));
}

/// @brief 
/// @param dec 
void DeviceSession::onProcessCmd(ProtoBufDec &dec) {
    typedef void (DeviceSession::*cmd_function)(ProtoBufDec &dec);
    static unordered_map<string, cmd_function> s_cmd_functions;
    static onceToken token([]() {
        s_cmd_functions.emplace("sessionReq", &DeviceSession::onMsg_sessionReq);
        s_cmd_functions.emplace("startOutput", &DeviceSession::onMsg_startProxyPush);
        s_cmd_functions.emplace("stopOutput", &DeviceSession::onMsg_stopProxyPush);
        s_cmd_functions.emplace("syncReq", &DeviceSession::onMsg_statusReq);
        s_cmd_functions.emplace("setPullAttr", &DeviceSession::onMsg_setPlayAttr);
    });

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

void DeviceSession::onMsg_sessionReq(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_sessionReq:" << dec.toString();
    const device::SessionReq &req = dec.messge()->sessionreq();
    if (req.sn().empty())
        return;

    //update session or insert a new session
    _device_helper = make_shared<DeviceHelper>(req.sn(), getPoller());
    auto &device = _device_helper->device();
    device->setAlive(true, true);

    //set event callback
    setEventHandle();

    //send sessionrsp to response
    GET_CONFIG(uint32_t, max_pushers, Mgw::kMaxPushers);
    GET_CONFIG(uint32_t, max_bitrate, Mgw::kMaxBitrate);
    GET_CONFIG(uint32_t, max_4kbitrate, Mgw::kMaxBitrate4K);
    GET_CONFIG(uint32_t, max_players, Mgw::kMaxPlayers);
    GET_CONFIG(string, tunnel_protocol, Mgw::kStreamTunnel);

    Device::DeviceConfig cfg(req.sn(), req.type(), req.vendor(),
                req.version(), req.accesstoken(), max_bitrate,
                max_4kbitrate, max_pushers, max_players);
    device->loadConfig(cfg);

    ////////////////// set response protobuf //////////////////
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::SessionRsp *rsp = msg->mutable_sessionrsp();

    rsp->set_accessresult(0);
    rsp->set_outputcapacity(max_pushers);
    rsp->set_maxbitrate(max_bitrate);
    rsp->set_maxbitrate4k(max_4kbitrate);
    common::StreamAddress *addr = rsp->mutable_streamaddr();
    string url = device->getPushaddr(0, tunnel_protocol, true);
    if (tunnel_protocol == "srt") {
        common::SRTStreamAddress *srt = addr->mutable_srt();
        srt->set_simaddr(url);
    } else {
        common::RTMPStreamAddress *rtmp = addr->mutable_rtmp();
        rtmp->set_uri(url);
    }
    rsp->set_pulladdr(device->getPlayaddr(0, tunnel_protocol, true));

    ProtoBufEnc enc(msg);
    sendResponse(enc);
    ///////////////////////////////////////////////////

    //设备会话注册上了，给u727发送设备上线通知
    cfg.play_addr = device->getPlayaddr(0, "rtsp");
    NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastDeviceOnline, true, cfg);
}

void DeviceSession::onMsg_startProxyPush(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_startProxyPush:" << dec.toString();
    if (!_device_helper.operator bool()) {
        WarnP(this) << "No SessionRequest before this message!";
        return;
    }

    const device::StartOutputStream &req = dec.messge()->startoutput();
    string out_name = getOutputName(false, req.outchn());
    string url;
    const common::StreamAddress &addr = req.address();
    if (addr.has_rtmp()) {
        url = addr.rtmp().uri();
        url.append(addr.rtmp().code());
    } else {
        url = addr.srt().simaddr();
    }

    GET_CONFIG(string, tunnel_protocol, Mgw::kStreamTunnel);
    auto device = _device_helper->device();
    MediaInfo info(device->getPushaddr(0, tunnel_protocol));

    //如果回调中需要对象的所有权，应该传入this,否则应该传入该对象的弱引用
    weak_ptr<DeviceSession> weak_self = dynamic_pointer_cast<DeviceSession>(shared_from_this());
    auto send_status = [weak_self, out_name](const string &name, int status, int err, int start_time){
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        MsgPtr msg = make_shared<mgw::MgwMsg>();
        device::OutputStreamStatus *rsp = msg->mutable_outputsta();
        rsp->set_srcchn(0);
        rsp->set_outchn(getOutputChn(name));
        rsp->set_status(status);
        rsp->set_starttime(start_time);
        rsp->set_lasterrcode(err);

        ProtoBufEnc enc(msg);
        strong_self->sendResponse(enc);

        //发送流状态变化事件，通知u727
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastStreamStatus, out_name, status, start_time, err);
    };

    auto on_status_changed = [weak_self, out_name, send_status, url](const string &name,
                    ChannelStatus status, Time_t time, const SockException &ex, void *userdata) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        if (status == ChannelStatus_Idle || ex) {
            ErrorP(strong_self.get()) << "pusher: " << out_name << " failed: " << ex.what();
            //推流失败，说明没有办法重推了，直接关闭这个推流
            strong_self->_device_helper->releasePusher(out_name);
        } else {
            //推流成功，发出一个推流成功事件，u727收到事件后发送通知消息
            NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastPush, true, strong_self->_device_helper->sn(), getOutputChn(name), url);
        }
        //成功与否，都要通知到device端
        send_status(name, (int)status, (int)ex.getCustomCode(), time);
    };

    auto push_task = [weak_self, out_name, url, on_status_changed](const MediaSource::Ptr &src) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        //执行推流任务
        if (src) {
            strong_self->_device_helper->addPusher(out_name, false, url, src, on_status_changed);
        } else {
            //过了一段时间还没有等到流，通知设备，推流失败
            WarnL << "没有找到需要的源";
            on_status_changed(out_name, ChannelStatus_Idle, 0, SockException(Err_timeout, "Err_timeout", -18), nullptr);
        }
    };
    //异步查找流，找到了就执行推流任务
    MediaSource::findAsync(info, shared_from_this(), push_task);
}

void DeviceSession::onMsg_stopProxyPush(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_stopProxyPush:" << dec.toString();
    if (!_device_helper.operator bool()) {
        WarnP(this) << "No SessionRequest before this message!";
        return;
    }

    const device::StopOutputStream &req = dec.messge()->stopoutput();
    string out_name = getOutputName(false, req.outchn());
    string url = _device_helper->device()->pusher(out_name)->getInfo().url;

    _device_helper->releasePusher(out_name);

    if (!url.empty()) {
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastPush, false, _device_helper->sn(), req.outchn(), url);
    }
}

void DeviceSession::onMsg_statusReq(ProtoBufDec &dec) {
    // DebugP(this) << "onMsg_statusReq:" << dec.toString();
    if (!_device_helper.operator bool()) {
        WarnP(this) << "No SessionRequest before this message!";
        return;
    }

    ////////////////// set protobuf //////////////////
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    device::SyncStatusRsp *rsp = msg->mutable_syncrsp();

    static uint32_t snd_cnt = 0;
    GET_CONFIG(uint32_t, max_pushers, Mgw::kMaxPushers);
    GET_CONFIG(uint32_t, max_bitrate, Mgw::kMaxBitrate);
    GET_CONFIG(uint32_t, max_4kbitrate, Mgw::kMaxBitrate4K);

    int currTs = chrono::duration_cast<chrono::seconds>\
                (chrono::system_clock::now().time_since_epoch()).count();

    rsp->set_devsn(_device_helper->sn());
    rsp->set_currts(currTs);
    rsp->set_sndcnt(++snd_cnt);
    rsp->set_chncap(max_pushers);
    rsp->set_maxbitrate(max_bitrate);
    rsp->set_maxbitrate4k(max_4kbitrate);
    rsp->set_players(_device_helper->players(true));
    rsp->set_playtotalbytes(_device_helper->playTotalBytes());

    _device_helper->pusher_for_each([rsp](PushHelper::Ptr pusher) {
        //在这里打包每个推流的信息
        device::StreamInfo *pb_info = rsp->add_streaminfos();
        StreamInfo ps_info = pusher->getInfo();
        pb_info->set_channel(ps_info.channel);
        pb_info->set_starttime(ps_info.startTime);
        pb_info->set_currenttime(::time(NULL));
        pb_info->set_stoptime(ps_info.stopTime);
        pb_info->set_totalbytessnd(ps_info.totalByteSnd);
        pb_info->set_reconnectcnt(ps_info.total_retry);
        pb_info->set_status((int)ps_info.status);
        common::StreamAddress *addr = pb_info->mutable_streamaddr();
        if (ps_info.url.substr(0, 7) == "rtmp://") {
            common::RTMPStreamAddress *rtmp = addr->mutable_rtmp();
            rtmp->set_uri(ps_info.url);
        } else if (ps_info.url.substr(0, 6) == "srt://") {
            common::SRTStreamAddress *srt = addr->mutable_srt();
            srt->set_simaddr(ps_info.url);
        }
    });

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

/////////////////////////////////////////////////////////////////////////////////////
static string getProtocol(uint32_t proto) {
    string ret;
    switch (proto) {
        case 1: ret = "rtmp"; break;
        case 2: ret = "srt"; break;
        case 3: ret = "udp"; break;
        case 4: ret = "rtsp"; break;
        default: break;
    }
    return ret;
}

void DeviceSession::onMsg_setPlayAttr(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_setPlayAttr:" << dec.toString();
    if (!_device_helper.operator bool()) {
        WarnP(this) << "No SessionRequest before this message!";
        return;
    }

    const device::SetPullAttr &req = dec.messge()->setpullattr();
    string proto = getProtocol(req.proto());
    _device_helper->enablePlayProtocol(req.enable(), req.forcestop(), proto);
}

}
