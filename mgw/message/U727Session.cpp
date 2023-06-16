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

U727Session::U727Session(const Socket::Ptr &sock)
    : Session(sock), MessageCodec(Entity_MgwServer),
      _u727(make_shared<U727>()) {
    DebugP(this);
}

U727Session::~U727Session() {

    if (_init_once) {
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
                strong_self->sendDeviceOnline(config.sn, config.type, config.version, config.vendor, config.play_addr);
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
        s_cmd_functions.emplace("queryStreamReq", &U727Session::onMsg_queryStreamReq);

    });

    if (!_init_once) {
        setEventHandle();
        _u727->u727Ready();
        _init_once = true;
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

void U727Session::startStream(const string &stream_id, uint32_t delay_ms,
                    StreamType src_type, const string &src_url,
                    StreamType dest_type, const string &dest_url,
                    onStatus on_status) {

    MediaInfo info;
    Device::Ptr device = nullptr;
    weak_ptr<U727Session> weak_self = dynamic_pointer_cast<U727Session>(shared_from_this());
    void *listen_tag = &info;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    //输入任务
    if (src_type == StreamType_Dev) {
        //源是设备的输入源,需要异步查找源，如果找不到，需要通知设备推流上来
        device = DeviceHelper::findDevice(src_url, false);
        if (!device) {
            WarnL << "Do not exist device: " << src_url;
            string err_des("Do not exist device: "); err_des.append(src_url);
            on_status(stream_id, -1, err_des, "", "");
            return;
        }

        GET_CONFIG(string, tunnel_protocol, Mgw::kStreamTunnel);
        string dev_src_url = device->getPushaddr(0, tunnel_protocol);
        info.parse(dev_src_url);
        if (info.getUrl().empty()) {
            WarnL << "Can't parse this source url: " << dev_src_url;
            return;
        }
    } else if (src_type == StreamType_Play || src_type == StreamType_File) {
        //源是网络拉流输入源，需要异步查找源，如果找不到，拉流输入，或者从文件输入
        string url = string("rtsp://") + string(DEFAULT_VHOST) + "/live/" + stream_id;
        info.parse(url);

        auto on_play_status = [weak_self, on_status](const string &id, ChannelStatus status, Time_t ts, const SockException &ex) {
            //输入源播放成功与否都要返回状态给u727
            DebugL << "play[" << id << "]: " << ex.what() << ", start_time: " << ts;
            // on_status(stream_id, err, des, "", "");
        };

        NoticeCenter::Instance().addListener(listen_tag, Broadcast::kBroadcastNotFoundStream,
            [weak_self, info, src_type, src_url, on_play_status](BroadcastNotFoundStreamArgs) {

            if (args._schema != info._schema ||
                args._vhost != info._vhost ||
                args._app != info._app ||
                args._streamid != info._streamid) {
                //不是我们感兴趣的流，忽略它
                return;
            }

            auto strong_self = weak_self.lock();
            if (!strong_self) { return; }

            //收到未找到流事件，切换回自己的线程再处理
            strong_self->getPoller()->async([weak_self, info, src_type, src_url, on_play_status](){
                auto strong_self = weak_self.lock();
                if (!strong_self) { return; }

                //此时去拉流或者从文件输入
                if (src_type == StreamType_Play || src_type == StreamType_File) {
                    auto player = strong_self->_u727->player(info._streamid);
                    if (player->status() != ChannelStatus_Idle) {
                        player->restart(src_url, on_play_status, nullptr, nullptr);
                    } else {
                        player->start(src_url, on_play_status, nullptr, nullptr);
                    }
                }
            }, false);
        });
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //输出任务
    auto on_push_status = [weak_self, stream_id, on_status](const string &name,
                ChannelStatus status, Time_t start_ts, const SockException &ex, void *userdata) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        string push_url, play_url;
        if (!ex && ChannelStatus_Idle != status) {
            push_url = strong_self->_u727->getRtspPushAddr(stream_id);
            play_url = strong_self->_u727->getRtspPullAddr(stream_id);
        }
        on_status(stream_id, ex.getCustomCode(), ex.what(), push_url, play_url);
    };

    auto do_output_task = [listen_tag, weak_self, stream_id, dest_type, dest_url, device, on_push_status, info](const MediaSource::Ptr &src) {
        //不管有没有找到流，这一次监听的时间先删除监听
        NoticeCenter::Instance().delListener(listen_tag, Broadcast::kBroadcastNotFoundStream);
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        int result = 0;
        string des;
        //执行推流任务
        if (src) {
            if (dest_type == StreamType_Publish) {
                //向指定url推流
                PushHelper::Ptr pusher = nullptr;
                if (device) {
                    pusher = device->pusher(stream_id);
                } else {
                    pusher = strong_self->_u727->pusher(stream_id);
                }

                if (pusher->status() != ChannelStatus_Idle) {
                    WarnL << "Already exist, release the old and create a new: " << stream_id;
                    pusher->restart(dest_url, on_push_status, src);
                } else {
                    pusher->start(dest_url, on_push_status, 15, src);
                }
            } else if (dest_type == StreamType_Play) {
                //需要根据stream_id生成拉流地址，返回给u727
                DebugL << "需要根据stream_id生成拉流地址";
                on_push_status(stream_id, ChannelStatus_Playing, ::time(NULL), SockException(), NULL);
            } else if (dest_type == StreamType_File) {
                //需要生成录像文件到指定路径
                DebugL << "需要生成录像文件到指定路径: " << dest_url;
                // MediaSource::createFromMP4(src->getSchema(), src->getVhost(), src->getApp(), src->getId(), dest_url);
                src->setupRecord(Recorder::type_mp4, true, dest_url, 30*60);
            }

        } else {
            //过了一段时间还没有等到流，通知设备，推流失败
            DebugL << "没有找到需要的源: " << info.getUrl();
            on_push_status(stream_id, ChannelStatus_Idle, ::time(NULL), SockException(Err_timeout, "Stream notfound", Common_Failed), NULL);
        }
    };
    //异步查找流，找到了就执行推流任务
    MediaSource::findAsync(info, shared_from_this(), do_output_task);
}

static void parseStartStreamReq(const u727::StartStreamReq &req,
            StreamType &src_type, string &src, StreamType &dest_type, string &dest) {

    switch (req.src_case()) {
        case u727::StartStreamReq::kSrcDevSn: {
            //请求设备推流上来
            src_type = StreamType_Dev;
            src = req.src_dev_sn();
            break;
        }
        case u727::StartStreamReq::kSrcPullAddr: {
            //mgw-server 需要从指定地址拉流作为输入源
            src_type = StreamType_Play;
            src = req.src_pull_addr();
            break;
        }
        case u727::StartStreamReq::kNeedPush: {
            //u727需要推流到mgw-server，此时返回一个推流地址给u727，根据stream_id生成地址
            src_type = StreamType_Publish;
            break;
        }
        case u727::StartStreamReq::kSrcFilePath: {
            //从指定文件输入
            src_type = StreamType_File;
            src = req.src_file_path();
            break;
        }
        default: break;
    }

    switch (req.dest_case()) {
        case u727::StartStreamReq::kDestPushAddr: {
            //向指定url推流
            dest_type = StreamType_Publish;
            dest = req.dest_push_addr();
            break;
        }
        case u727::StartStreamReq::kNeedPull: {
            //本地监听等待拉流播放，需要返回播放地址给u727，根据stream_id生成地址
            dest_type = StreamType_Play;
            break;
        }
        case u727::StartStreamReq::kDestFilePath: {
            //输入到指定文件
            dest_type = StreamType_File;
            dest = req.dest_file_path();
            break;
        }
        default: break;
    }
}

void U727Session::onMsg_startStreamReq(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_startStreamReq:" << dec.toString();

    string src, dest;
    StreamType src_type = StreamType_None, dest_type = StreamType_None;
    const u727::StartStreamReq &req = dec.messge()->startstreamreq();

    parseStartStreamReq(req, src_type, src, dest_type, dest);

    weak_ptr<U727Session> weak_self = dynamic_pointer_cast<U727Session>(shared_from_this());
    auto on_status = [weak_self, src_type, dest_type](const string &stream_id, int res, const string &des,
                                const string &push_url, const string &play_url) {
        auto strong_self = weak_self.lock();
        if (!strong_self) { return; }

        MsgPtr msg = make_shared<mgw::MgwMsg>();
        u727::StartStreamReply *reply = msg->mutable_startstreamreply();
        reply->set_stream_id(stream_id);
        reply->set_result(res);
        reply->set_descrip(des);
        if (src_type == StreamType_Publish) {
            reply->set_push_url(push_url);
        }
        if (dest_type == StreamType_Play) {
            reply->set_pull_url(play_url);
        }

        ProtoBufEnc enc(msg);
        strong_self->sendResponse(enc);
    };

    if (src_type == StreamType_None || dest_type == StreamType_None) {
        WarnL << "Source type: " << src_type << ", Destination type: " << dest_type;
        on_status(req.stream_id(), -1, "Error stream type", "", "");
        return;
    }

    startStream(req.stream_id(), req.delay_ms(), src_type, src, dest_type, dest, on_status);
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
        //给u727返回一个设备流的rtsp拉流地址
        info->set_stream_url(device->getPlayaddr(0, "rtsp"));
    });

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

void U727Session::onMsg_queryStreamReq(ProtoBufDec &dec) {
    DebugP(this) << "onMsg_queryStreamReq: " << dec.toString();

    ////////////////////////////////////////////////////////////////
    ///response: queryStreamReply
    MsgPtr msg = make_shared<mgw::MgwMsg>();
    u727::QueryStreamStatusReply *reply = msg->mutable_querystreamreply();

    auto pack_status = [reply](const StreamInfo &info, bool input) {
        u727::StreamStatusNotify *status = reply->add_streams_status();
        status->set_stream_id(info.id);
        status->set_status(info.status);
        status->set_starttime(info.startTime);
        status->set_lasterrcode(0);
        status->set_stream_type(input ? 1 : 2);
    };

    //先打包u727的输入，输出流
    _u727->player_for_each([pack_status](PlayHelper::Ptr player){
        pack_status(move(player->getInfo()), true);
    });
    _u727->pusher_for_each([pack_status](PushHelper::Ptr pusher){
        pack_status(move(pusher->getInfo()), false);
    });

    //打包所有设备的输入和输出
    DeviceHelper::device_for_each([pack_status](Device::Ptr device){

        device->player_for_each([pack_status](PlayHelper::Ptr player){
            pack_status(move(player->getInfo()), false);
        });
        device->pusher_for_each([pack_status](PushHelper::Ptr pusher){
            pack_status(move(pusher->getInfo()), false);
        });
    });

    DebugP(this) << "StreamStatus reply: " << msg->DebugString();

    ProtoBufEnc enc(msg);
    sendResponse(enc);
}

}
