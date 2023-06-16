#include "MessageClient.h"
#include "core/UcastDevice.h"
#include "Common/config.h"
#include "Util/onceToken.h"
#include "mgw.pb.h"

using namespace std;
using namespace toolkit;
using namespace mediakit;

namespace MGW {

MessageClient::MessageClient(const EventPoller::Ptr &poller,
        Entity entity, const DeviceHelper::Ptr &ptr)
        : MessageCodec(entity) {
    setPoller(poller ? move(poller) : EventPollerPool::Instance().getPoller());
    if (entity == Entity_Device) {
        _device_helper = ptr;
    }
}

MessageClient::~MessageClient() {
    DebugP(this) << "release message client";
}

//发送消息，先保存在缓存中
int MessageClient::sendMessage(const char *msg, size_t size) {
    SockSender::send(msg, size);
    return 0;
}

int MessageClient::sendMessage(const string &msg) {
    SockSender::send(msg);
    return 0;
}

//internal
//------------------------------------------------------------------------
void MessageClient::onRecv(const Buffer::Ptr &pBuf) {
    onParseMessage(pBuf->data(), pBuf->size());
}

void MessageClient::setOnConnectErr(const onConnectErr &func) {
    _on_connect_err = func;
}

//被动断开连接回调，这里应该设置一个定时器去尝试重连，每次尝试等待时长应该增加
void MessageClient::onErr(const SockException &ex) {
    //应该回调到上层，做重连或者销毁处理
    if (_on_connect_err) {
        _on_connect_err(ex);
    }
}

//tcp连接成功后每2秒触发一次该事件
void MessageClient::onManager() {
    // SendMessage("Hello mgw-server!");
}

//连接服务器结果回调,连接成功和失败都会回调
void MessageClient::onConnect(const SockException &ex) {
    //连接成功的时候，需要发送一个sessionReq请求，相关参数我们可以从deviceHelper中获取到
    if (!ex) {
        auto strong_device = _device_helper.lock();
        if (!strong_device) {
            WarnP(this) << "No device instance!";
            return;
        }
        Device::DeviceConfig cfg = strong_device->device()->getConfig();
        sendSession(cfg.sn, cfg.type, cfg.version, cfg.vendor,
            cfg.access_token,cfg.max_pushers, cfg.max_bitrate, cfg.max_4kbitrate);
    } else {
        if (_on_connect_err) {
            _on_connect_err(ex);
        }
    }
}

//数据全部发送完毕后回调
void MessageClient::onFlush() {
    DebugL << "Send data onFlush!";
}

//////////////////////////////////////////////////////////////////
void MessageClient::onMessage(MessagePacket::Ptr msg_pkt) {
    //1.根据MessagePacket的负载，生成一个ProtoBufDec对象
    ProtoBufDec dec(msg_pkt->_buffer);
    //2.交给onProcessCmd处理
    onProcessCmd(dec);
}

void MessageClient::onSendRawData(toolkit::Buffer::Ptr buffer) {
    sendMessage(buffer->data(), buffer->size());
}

void MessageClient::onProcessCmd(ProtoBufDec &dec) {
    typedef void (MessageClient::*cmd_function)(ProtoBufDec &dec);
    static unordered_map<string, cmd_function> s_cmd_functions;
    static onceToken token([]() {
        s_cmd_functions.emplace("sessionRsp", &MessageClient::onMsg_sessionRsp);
        s_cmd_functions.emplace("response", &MessageClient::onMsg_commonRsp);
        s_cmd_functions.emplace("outputSta", &MessageClient::onMsg_outputStatus);
        s_cmd_functions.emplace("syncRsp", &MessageClient::onMsg_statusRsp);
        s_cmd_functions.emplace("pushStreamReq", &MessageClient::onMsg_startTunnelPush);
        s_cmd_functions.emplace("stopPushing", &MessageClient::omMsg_stopTunnelPush);

        //////////////////////////////////////////////////////////
        s_cmd_functions.emplace("serSessionRsp", &MessageClient::onMsg_ServerSessionRsp);
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

void MessageClient::onMsg_sessionRsp(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_sessionRsp: " << dec.toString();
    const device::SessionRsp &rsp = dec.messge()->sessionrsp();

    auto strong_dev_helper = _device_helper.lock();
    if (strong_dev_helper) {
        Device::DeviceConfig cfg = strong_dev_helper->device()->getConfig();
        //1.如果已经存在比配置的通道数还要大的推流(开机自动推流可能会出现这种情况)，应该停止它
        if (rsp.outputcapacity() < cfg.max_pushers) {
            for (auto chn = rsp.outputcapacity(); chn < cfg.max_pushers; chn++) {
                strong_dev_helper->releasePusher(getOutputName(true, chn));
            }
        }
        //2.提取会话回复中相关配置字段，设置到设备属性中，由设备做相关的回调处理
        cfg.max_pushers = rsp.outputcapacity();
        // cfg.max_players = rsp.maxplayers();
        cfg.max_bitrate = rsp.maxbitrate();
        cfg.max_4kbitrate = rsp.maxbitrate4k();
        //解析出推拉流地址
        cfg.play_addr = rsp.pulladdr();
        const common::StreamAddress &addr = rsp.streamaddr();
        if (addr.has_rtmp()) {
            cfg.push_addr = addr.rtmp().uri();
            cfg.push_addr.append(addr.rtmp().code());
        } else {
            cfg.push_addr = addr.srt().simaddr();
        }
        //config发生了变化，应该加载到设备实例中，并且通知到外部
        strong_dev_helper->device()->loadConfig(cfg);
        strong_dev_helper->device()->doOnConfigChanged(cfg);
    }

    //3.如果定时器没有开启，则开启定时器，在interval间隔发送一次状态同步请求,
    //定时器和MessageClient在同一个线程上，避免不同线程竞争
    if (!_ka_timer) {
        weak_ptr<MessageClient> weak_self = dynamic_pointer_cast<MessageClient>(shared_from_this());
        if (!_ka_sec) {
            _ka_sec = mINI::Instance()[WsCli::kPingSec];
        }
        _ka_timer = make_shared<Timer>((float)_ka_sec, [weak_self]()->bool {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return false;
            }

            strong_self->sendStatusReq();
            //返回true，一直重复发送状态同步消息
            return true;
        }, getPoller());
    }
}

void MessageClient::onMsg_commonRsp(ProtoBufDec &dec) {
    //1.如果是代理推流失败了，告诉设备释放这个代理推流实例
    const device::CommonRsp &rsp = dec.messge()->response();
    if (rsp.command() == CmdType_StartPush && rsp.result() != 0) {
        auto strong_dev_helper = _device_helper.lock();
        if (strong_dev_helper) {
            strong_dev_helper->releasePusher(getOutputName(true, rsp.outchn()));
        }
    }
}

//推流真实状态
void MessageClient::onMsg_outputStatus(ProtoBufDec &dec) {
    //调用设备状态设置，并回调
    const device::OutputStreamStatus &rsp = dec.messge()->outputsta();
    auto strong_dev_helper = _device_helper.lock();
    if (strong_dev_helper) {
        strong_dev_helper->device()->doOnStatusChanged(
                ChannelType_ProxyOutput, rsp.outchn(),
                ChannelStatus(rsp.status()), ErrorCode(rsp.lasterrcode()),
                Time_t(rsp.starttime()));
    }
}

void MessageClient::onMsg_statusRsp(ProtoBufDec &dec) {
    // DebugP(this) << "onMsg_statusRsp: " << dec.toString();
    //状态同步回复，如果有已经停止了的流，应该释放
    auto strong_dev_helper = _device_helper.lock();
    if (!strong_dev_helper) {
        WarnP(this) << "No device!";
        return;
    }

    const device::SyncStatusRsp &rsp = dec.messge()->syncrsp();
    for (int i = 0; i < rsp.streaminfos_size(); i++) {
        const device::StreamInfo &info = rsp.streaminfos(i);

        strong_dev_helper->device()->doOnStatusChanged(ChannelType_ProxyOutput,
                                info.channel(), ChannelStatus(info.status()),
                                Success, info.starttime());
    }

    strong_dev_helper->device()->doOnPlayersChange(false, rsp.players());
}

void MessageClient::onMsg_startTunnelPush(ProtoBufDec &dec) {
    //调用设备实例，开启一个推流到指定地址，一般是mgw-server
    const device::PushStreamReq &req = dec.messge()->pushstreamreq();

    weak_ptr<MessageClient> weak_self = dynamic_pointer_cast<MessageClient>(shared_from_this());
    auto on_register = [weak_self](const MediaSource::Ptr &src){
        auto strong_self = weak_self.lock();
        if (!strong_self) { return; }

        auto strong_dev_helper = strong_self->_device_helper.lock();
        if (strong_dev_helper) {
            strong_dev_helper->startTunnelPusher(src);
        }
    };

    auto strong_dev = _device_helper.lock();
    if (!strong_dev) {
        return;
    }

    GET_CONFIG(string, schema, Mgw::kStreamTunnel);
    ostringstream oss;
    //这里暂时设置成使用设备物理输入 TODO:消息可以指定某个源传输到服务器
    string streamid = getStreamId(strong_dev->sn(), Location_Dev, InputType_Phy, req.chn());
    oss << schema << "://" << DEFAULT_VHOST << "/live/" << streamid;
    DebugL << "Tunnel source url: " << oss.str();

    MediaInfo info(oss.str());
    MediaSource::findAsync(info, shared_from_this(), on_register);
}

void MessageClient::omMsg_stopTunnelPush(ProtoBufDec &dec) {
    //调用设备实例，停止一个指定的推流，一般是设备到mgw-server的流
    const device::StopPushingStream &req = dec.messge()->stoppushing();
    auto strong_dev_helper = _device_helper.lock();
    if (strong_dev_helper) {
        strong_dev_helper->stopTunnelPusher();
    }
}

/// @brief ////////////////////////////////////////////////
/// @param dec 
void MessageClient::onMsg_ServerSessionRsp(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_ServerSessionRsp: " << dec.toString();

    //如果建立会话成功，那么需要开启定时器，定时发送状态同步
    const device::ServerSessionRsp &rsp = dec.messge()->sersessionrsp();
    if (rsp.sessionresult() == ErrorCode::Success && !_ka_timer) {
        weak_ptr<MessageClient> weak_self = dynamic_pointer_cast<MessageClient>(shared_from_this());

        _ka_timer = make_shared<Timer>(10, [weak_self]()->bool {
            ////////////////// set protobuf //////////////////
            static uint32_t snd_cnt = 0;
            int currTs = chrono::duration_cast<chrono::seconds>\
                        (chrono::system_clock::now().time_since_epoch()).count();

            DeviceHelper::device_for_each([weak_self, currTs](Device::Ptr device) {
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return;
                }

                MsgPtr msg = make_shared<mgw::MgwMsg>();
                device::SyncStatusRsp *rsp = msg->mutable_syncrsp();
                auto cfg = device->getConfig();

                rsp->set_devsn(cfg.sn);
                rsp->set_currts(currTs);
                rsp->set_sndcnt(++snd_cnt);
                rsp->set_chncap(cfg.max_pushers);
                rsp->set_maxbitrate(cfg.max_bitrate);
                rsp->set_maxbitrate4k(cfg.max_4kbitrate);
                rsp->set_players(device->players(true, "") + device->players(false, ""));
                //TODO:获取设备总共播放服务流量，待实现
                rsp->set_playtotalbytes(0);

                device->pusher_for_each([rsp](PushHelper::Ptr pusher) {
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
                strong_self->sendResponse(enc);
            });
            //返回true，一直重复发送状态同步消息
            return true;
        }, getPoller());
    }
}

}