#include "mk_mgw.h"
#include "core/UcastDevice.h"
#include "message/MessageClient.h"
#include "Util/onceToken.h"
#include "Util/logger.h"
#include "Common/config.h"
#include "Network/TcpServer.h"
#include "Network/UdpServer.h"
#include "Poller/EventPoller.h"
#include "Rtsp/RtspSession.h"
#include "Rtmp/RtmpSession.h"
#include "Shell/ShellSession.h"
#include "Http/WebSocketSession.h"
#include "Rtp/RtpServer.h"
#include "Common/Device.h"
#include "core/EventProcess.h"

#include <exception>
#include <assert.h>

using namespace std;
using namespace toolkit;
using namespace mediakit;

//TODO: 注意，为了防止线程竞争问题，所有的操作都要转移到本线程_poller上来

struct PusherInfo {
    string url;
    string token;
    bool remote;
    bool channel;
    string name;
    string netif;
    uint16_t mtu;
    void *userdata;
    PusherInfo(pusher_info *info) {
        url = info->url;
        //去掉url后面的'/'
        if (url.back() == '/') {
            url.pop_back();
        }
        if (info->key) {
            //去掉key前面的'/'
            if (info->key[0] == '/') {
                info->key += 1;
            }
            url += string("/") + info->key;
        }

        remote = info->remote;
        channel = info->pusher_chn;
        name = getOutputName(info->remote, info->pusher_chn);
        netif = info->bind_netif?info->bind_netif:"";
        mtu = info->bind_mtu;
        userdata = info->user_data;
    }
};

class UcastSourceHelper : public MediaSourceEvent, public enable_shared_from_this<UcastSourceHelper> {
public:
    using Ptr = shared_ptr<UcastSourceHelper>;

    UcastSourceHelper(const string &vhost, const string &app, const string &id, const EventPoller::Ptr &poller);
    ~UcastSourceHelper();

    DevChannel::Ptr &getChannel() { return _channel; }

    //添加音视频，完成后调用addTrackCompleted
    void initSource(source_info *src_info);
    void initLocalSource(stream_meta *info);
    void initPlaySource(play_info *info);

    bool initVideo(stream_meta *info);
    bool initAudio(stream_meta *info);

    string getStreamId() { return _stream_id; }

private:
    bool                _local_source;
    string              _stream_id;
    EventPoller::Ptr    _poller;
    DevChannel::Ptr     _channel;
};

class DeviceHandle : public enable_shared_from_this<DeviceHandle> {
public:
    using Ptr = shared_ptr<DeviceHandle>;
    using clientPtr = WebSocketClient<MessageClient, WebSocketHeader::BINARY>::Ptr;

    DeviceHandle();
    ~DeviceHandle();
    //system
    bool startup(mgw_context *ctx);

    //message
    void connectServer(const conn_info *info);
    void disconnect();
    bool messageAvailable();
    //source
    bool hasSource(bool local, int channel);
    bool addRawSource(source_info *info);
    void releaseRawSource(bool local, int channel);
    void inputFrame(uint32_t channel, const mk_frame_t &frame);
    void updateMeta(bool local, int channel, stream_meta *info);
    bool setupRecord(bool local, int channel, bool start = true);

    //pusher
    int addPusher(pusher_info *info);
    void releasePusher(bool remote, int channel);
    bool hasPusher(bool remote, int channel);
    bool hasTunnelPusher();
    void releaseTunnelPusher();

    //player
    int addPlayer(player_attr info);
    void releasePlayer(bool remote, int channel);

    //play service
    void setPlayService(play_service_attr *attr);
    void getPlayService(play_service_attr *attr);

private:
    void setDeviceCallback();

private:
    bool                _have_session_rsp = false;
    mk_codec_id         _video_eid = MK_CODEC_ID_NONE;
    //线程管理
    EventPoller::Ptr    _poller = nullptr;
    //设备管理
    DeviceHelper::Ptr   _device_helper = nullptr;
    //tcp 服务，rtsp，rtmp，http
    unordered_map<uint16_t, TcpServer::Ptr> _svr_map;
    //媒体源管理
    UcastSourceHelper::Ptr _source_helper = nullptr;
    /*----------------------------------------------*/
    mgw_callback        _device_cb = NULL;
    /*-----------------ws信息----------------------------*/
    uint16_t            _ka_sec;
    uint16_t            _mtu;
    string              _netif;
    string              _url;
    clientPtr           _ws_client = nullptr;
};

/**-------------------------------------------------*/
//全局设备管理器
static DeviceHandle::Ptr *obj = nullptr;

//-------------------------------------------------------------------------------
UcastSourceHelper::UcastSourceHelper(const string &vhost, const string &app,
                const string &id, const EventPoller::Ptr &poller)
                : _local_source(true), _stream_id(id) {
    _poller = poller ? poller : EventPollerPool::Instance().getPoller();
    _poller->sync([&]() { _channel = make_shared<DevChannel>(vhost, app, id); });
}
UcastSourceHelper::~UcastSourceHelper() {

}

bool UcastSourceHelper::initVideo(stream_meta *info) {
    VideoInfo vinfo;
    if (info->video_id == MK_CODEC_ID_H264) {
        vinfo.codecId = CodecH264;
    } else if (info->video_id == MK_CODEC_ID_H265) {
        vinfo.codecId = CodecH265;
    } else {
        WarnL << "Not support this video codecid: " << info->video_id;
        return false;
    }

    vinfo.iFrameRate = info->fps;
    vinfo.iWidth = info->width;
    vinfo.iHeight = info->height;
    vinfo.iBitRate = info->vkbps*1000;
    DebugL << "Add video track id: " << vinfo.codecId << " fps: " <<
                vinfo.iFrameRate << " width: " << vinfo.iWidth << " height: " << 
                vinfo.iHeight << " bitrate: " << vinfo.iBitRate;

    return _channel->initVideo(vinfo);
}

bool UcastSourceHelper::initAudio(stream_meta *info) {
    //加载音频
    if (info->audio_id != MK_CODEC_ID_AAC) {
        WarnL << "Not support this audio codecid: " << info->audio_id;
        return false;
    }

    AudioInfo ainfo;
    ainfo.codecId = CodecAAC;
    ainfo.iSampleRate = info->samplerate;
    ainfo.iChannel = info->channels;
    ainfo.iSampleBit = info->samplesize;
    DebugL << "Add audio track id: " << ainfo.codecId << " chn: " <<
            ainfo.iChannel << " sr: " << ainfo.iSampleRate << " sb: " << ainfo.iSampleBit;
    _channel->initAudio(ainfo);
    return true;
}

void UcastSourceHelper::initLocalSource(stream_meta *info) {
    _local_source = true;
    initVideo(info);
    initAudio(info);
    //完成音视频加载, 其实不需要手动设置完成音视频加载注册，内部会根据有没有音视频帧输入判断
    //如果音视频都有帧输入了，会自动注册source
    _channel->addTrackCompleted();
}

void UcastSourceHelper::initPlaySource(play_info *info) {
    _local_source = false;
    //使用拉流播放作为输入源
}

//添加音视频，完成后调用addTrackCompleted
void UcastSourceHelper::initSource(source_info *src_info) {
    src_info->local ? 
        initLocalSource(&src_info->local_src) :
        initPlaySource(&src_info->play_src);
}

//-------------------------------------------------------------------------------

DeviceHandle::DeviceHandle() {
    _poller = EventPollerPool::Instance().getPoller();
}

DeviceHandle::~DeviceHandle() {
    EventProcess::Instance()->exit();
}

void DeviceHandle::setDeviceCallback() {
    //回调给设备的一些事件，比如，流状态变化，水线变化，查询指定网络状态(tun0)，通知设备配置
    //查询指定网卡是否可用，如果可用，返回正常网卡名和mtu
    _device_helper->setOnNetif([&](std::string &netif, uint16_t &mtu){
        if (_device_cb) {
            netinfo info;
            _device_cb((mgw_handler_t*)obj, MK_EVENT_GET_NETINFO, (void*)&info);
            netif = info.netif;
            netif = info.mtu;
        }
    });
    //设备配置有变化，与服务器协商后，按照服务器能力改变
    _device_helper->setOnConfigChanged([&](const Device::DeviceConfig &cfg){
        if (_device_cb) {
            stream_attrs attrs;
            attrs.max_pushers = cfg.max_pushers;
            attrs.max_players = cfg.max_players;
            attrs.max_bitrate = cfg.max_bitrate;
            attrs.max_4kbitrate = cfg.max_4kbitrate;
            _device_cb((mgw_handler_t*)obj, MK_EVENT_SET_STREAM_CFG, (void*)&attrs);
        }
        //收到会话回复才会调用这个回调，说明会话请求已经被回复了，可以进行后面的操作了
        _have_session_rsp = true;
    });
    //观看人数发生变化，应该注意判断是 设备本地的服务 还是 mgw-server的服务
    _device_helper->setOnPlayersChange([&](bool local, int players) {
        // DebugL << "on " << (local ? "local " : "remote ") << "players changed: " << players;
    });
    //推流或者拉流找不到流，可以通知启动设备编码器，按需编码
    _device_helper->setOnNotFoundStream([&](const std::string &url) {
        string name = _source_helper->getStreamId();
        if (url.find(name) == string::npos) {
            return;
        }
        //切换回自己的线程再执行
        _poller->async([&, name](){
            if (_device_cb) {
                int chn = getSourceChn(name);
                _device_cb((mgw_handler_t*)obj, MK_EVENT_SET_START_VIDEO, &chn);
            }
        }, false);
    });
    //远程代理流状态发生变化时走这个回调
    _device_helper->setOnStatusChanged([&](ChannelType chn_type, ChannelId chn,
                        ChannelStatus status, ErrorCode err, Time_t start_ts, void *userdata) {
        if (_device_cb &&
            (ChannelType_Output == chn_type ||
             ChannelType_ProxyOutput==chn_type)) {
            status_info info;
            info.remote = ChannelType_ProxyOutput==chn_type;
            info.channel = chn;
            info.status = (mk_status)status;
            info.error = err;
            info.start_time = start_ts;
            info.priv = userdata;

            _device_cb((mgw_handler_t*)obj, MK_EVENT_SET_STREAM_STATUS, (void*)&info);
        }
    });
    //流已经没有消费者了，此时可以通知编码器停止编码
    _device_helper->setOnNoReader([&](){
        DebugL << "On no reader";
    });
    //推拉流鉴权处理，设备上的推拉流不做鉴权，直接通过
    _device_helper->setOnAuthen([&](const string &url)->bool{
        return true;
    });

}

bool DeviceHandle::startup(mgw_context *ctx) {
    assert(ctx);

    int level = ctx->log_level;
    string log_path(ctx->log_path);
    _device_cb = ctx->callback;

    //确保只初始化一次
    static onceToken token([&, level, log_path]() {
        //日志文件
        auto channel = make_shared<FileChannel>("FileChannel",
                            !log_path.empty() ? log_path :
                            exeDir() + "log/", (LogLevel)level);
        //最多保存7天的日志
        channel->setMaxDay(7);
        Logger::Instance().add(channel);
        //异步日志线程
        Logger::Instance().setWriter(make_shared<AsyncLogWriter>());
        //设置线程数，设置成0会根据cpu核心数创建线程
        EventPollerPool::setPoolSize(0);
        WorkThreadPool::setPoolSize(0);

        try {
            mINI::Instance().parseFile();
        } catch(exception &e) {
            WarnL << "Parse ini file failed!";
            mINI::Instance().dumpFile();
        }

        // if (ssl && ssl[0]) {
        //     //设置ssl证书
        //     SSL_Initor::Instance().loadCertificate(ssl, true, ssl_pwd ? ssl_pwd : "", ssl_is_path);
        // }
    });

    //初始化设备实例
    _device_helper = make_shared<DeviceHelper>(ctx->sn, _poller);
    auto device = _device_helper->device(); //一定会成功
    Device::DeviceConfig cfg(ctx->sn, ctx->type, ctx->vendor, ctx->version,
                            "", ctx->stream_attr.max_bitrate, ctx->stream_attr.max_4kbitrate,
                            ctx->stream_attr.max_pushers, ctx->stream_attr.max_players);
    device->loadConfig(cfg);
    setDeviceCallback();

    //初始化websocket客户端
    using Ws = WebSocketClient<MessageClient, WebSocketHeader::BINARY>;
    _ws_client = make_shared<Ws>(_poller, Entity_Device, _device_helper);

    //要设置连接失败回调，失败时，延时一段时间后继续尝试连接
    weak_ptr<DeviceHandle> weak_self = shared_from_this();
    _ws_client->setOnConnectErr([weak_self](const SockException &ex){
        auto strong_self = weak_self.lock();
        if (!strong_self) { return; }

        if (ex && !strong_self->_ws_client->alive()) {
            strong_self->_poller->doDelayTask(2*1000, [weak_self]()->uint64_t {
                auto strong_self = weak_self.lock();
                if (!strong_self) { return 0; }

                if (!strong_self->_netif.empty()) {
                    strong_self->_ws_client->setNetAdapter(strong_self->_netif, strong_self->_mtu);
                }
                if (strong_self->_ka_sec) {
                    strong_self->_ws_client->setKaSec(strong_self->_ka_sec);
                }
                strong_self->_ws_client->startWebSocket(strong_self->_url);
                return 0;
            });
        }
    });

    //初始化协议服务
    uint16_t rtspPort = mINI::Instance()[Rtsp::kPort];
    uint16_t rtspsPort = mINI::Instance()[Rtsp::kSSLPort];
    uint16_t rtmpPort = mINI::Instance()[Rtmp::kPort];
    uint16_t rtmpsPort = mINI::Instance()[Rtmp::kSSLPort];
    uint16_t httpPort = mINI::Instance()[Http::kPort];
    uint16_t httpsPort = mINI::Instance()[Http::kSSLPort];
    // uint16_t rtpPort = mINI::Instance()[RtpProxy::kPort];

    try {
        _svr_map.emplace(rtspPort, make_shared<TcpServer>());
        _svr_map.emplace(rtspsPort, make_shared<TcpServer>());
        _svr_map.emplace(rtmpPort, make_shared<TcpServer>());
        _svr_map.emplace(rtmpsPort, make_shared<TcpServer>());
        _svr_map.emplace(httpPort, make_shared<TcpServer>());
        _svr_map.emplace(httpsPort, make_shared<TcpServer>());

        //rtsp服务器，端口默认554
        if (rtspPort) { _svr_map[rtspPort]->start<RtspSession>(rtspPort); }
        //rtsps服务器，端口默认322
        if (rtspsPort) { _svr_map[rtspsPort]->start<RtspSessionWithSSL>(rtspsPort); }
        //rtmp服务器，端口默认1935
        if (rtmpPort) { _svr_map[rtmpPort]->start<RtmpSession>(rtmpPort); }
        //rtmps服务器，端口默认19350
        if (rtmpsPort) { _svr_map[rtmpsPort]->start<RtmpSessionWithSSL>(rtmpsPort); }
        //http服务器，端口默认80
        if (httpPort) { _svr_map[httpPort]->start<HttpSession>(httpPort); }
        //https服务器，端口默认443
        if (httpsPort) { _svr_map[httpsPort]->start<HttpsSession>(httpsPort); }

    } catch (exception &ex) {
        WarnL << "端口占用或无权限:" << ex.what() << endl;
        ErrorL << "程序启动失败，请修改配置文件中端口号后重试!" << endl;
        _svr_map.clear();
        return false;
    }

    //在这里监听事件，触发hook调用，比如鉴权，注册和注销流 事件
    EventProcess::Instance()->run();

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
///message
void DeviceHandle::connectServer(const conn_info *info) {
    if (!_ws_client || _ws_client->alive()) {
        return;
    }
    //设置网络接口
    if (info->netif) {
        _mtu = info->mtu;
        _netif = info->netif;
        _ws_client->setNetAdapter(info->netif, info->mtu);
    }
    if (info->ka_sec) {
        _ka_sec = info->ka_sec;
        _ws_client->setKaSec(info->ka_sec);
    }

    _device_helper->device()->updateToken(info->token);
    //连接到mgw-server
    _url = info->url;
    _ws_client->startWebSocket(info->url);
}

//主动关闭连接，不会尝试重连
void DeviceHandle::disconnect() {
    _ws_client->shutdown();
}

bool DeviceHandle::messageAvailable() {
    return _ws_client && _ws_client->alive() && _have_session_rsp;
}

/////////////////////////////////////////////////////////////////////////////////////
///source
bool DeviceHandle::hasSource(bool local, int channel) {
    return !!_source_helper;
}

bool DeviceHandle::addRawSource(source_info *info) {
    if (!_source_helper) {
        string src_name = getSourceName(!info->local, info->channel, _device_helper->sn());
        _source_helper = make_shared<UcastSourceHelper>(DEFAULT_VHOST, "live", src_name, _poller);
    }

    // _source_helper->initSource(info);
    if (info->local) {
        _video_eid = info->local_src.video_id;
    }

    return true;
}

void DeviceHandle::releaseRawSource(bool local, int channel) {

}

bool DeviceHandle::setupRecord(bool local, int channel, bool start) {
    string id = getSourceName(!local, channel, _device_helper->sn());
    if (local) {
        auto src = MediaSource::find(DEFAULT_VHOST, "live", id);
        if (!src) {
            WarnL << "Not found the source:" << id;
            return false;
        }
        return src->setupRecord(Recorder::type_mp4, start, "./record/", 60*60);
    } else {
        //TODO: 应该发送消息到mgw-server服务器开启/关闭录像
        return false;
    }
}

void DeviceHandle::inputFrame(uint32_t channel, const mk_frame_t &frame) {
    auto chn  = _source_helper->getChannel();
    if (!chn) {
        return;
    }
    if (frame.id != MK_CODEC_ID_AAC && frame.id != _video_eid) {
        WarnL << "frame eid: " << frame.id << " old eid:" << _video_eid;
        return;
    }

    switch (frame.id) {
        case MK_CODEC_ID_H264:
        case MK_CODEC_ID_H265: {
            if (!chn->getTrack(TrackVideo, false) && !chn->getTrack(TrackVideo, true) && _device_cb) {
                stream_meta meta = {};
                _device_cb((mgw_handler_t*)obj, MK_EVENT_GET_STREAM_META, (void*)&meta);
                if (!_source_helper->initVideo(&meta)) {
                    return;
                }
            }
            frame.id == MK_CODEC_ID_H264 ?
                    chn->inputH264(frame.data, frame.size, frame.dts, frame.pts) :
                    chn->inputH265(frame.data, frame.size, frame.dts, frame.pts);
            break;
        }
        case MK_CODEC_ID_AAC: {
            if (!chn->getTrack(TrackAudio, false) && !chn->getTrack(TrackAudio, true) && _device_cb) {
                stream_meta meta = {};
                _device_cb((mgw_handler_t*)obj, MK_EVENT_GET_STREAM_META, (void*)&meta);
                if (!_source_helper->initAudio(&meta)) {
                    return;
                }
            }
            //adts头一般是7字节长度,设备输入的aac帧带有adts头
            chn->inputAAC(frame.data+7, frame.size-7, frame.dts, frame.data);
            break;
        }
        default: {
            WarnL << "Not support this encoder id: " << frame.id;
            break;
        }
    }
}

void DeviceHandle::updateMeta(bool local, int channel, stream_meta *info) {
    auto chn = _source_helper->getChannel();
    if (info->video_id != _video_eid) {
        _video_eid = info->video_id;
        //reset 所有的track，原来所有的输出就会失效，所以在还有消费者的时候，不允许更改编码器类型
        chn->resetTracks();
        //需要重新初始化
        _source_helper->initLocalSource(info);
    }
}

//pusher
int DeviceHandle::addPusher(pusher_info *info) {
    string src_name = getSourceName(info->src_remote, info->src_chn, _device_helper->sn());
    PusherInfo pusher_info(info);
    ostringstream oss;
    //注意，如果子串开头就是字符串开头，不要填入
    oss << FindField(info->url, NULL, "://") << "://" << DEFAULT_VHOST << "/live/" << src_name;
    MediaInfo media_info(oss.str());
    weak_ptr<DeviceHandle> weak_self = shared_from_this();

    if (!info->remote) {
        //TODO: 1.增加用户名和密码鉴权方式，2.增加ip地址直连方式(把vhost替换成ip)
        auto on_status_changed = [weak_self, pusher_info](const string &name, ChannelStatus status,
                        Time_t start_ts, const SockException &ex, void *userdata){
            auto strong_self = weak_self.lock();
            if (!strong_self) { return; }

            if (strong_self->_device_cb) {
                status_info new_status;
                new_status.channel = pusher_info.channel;
                new_status.remote = pusher_info.remote;
                new_status.error = (ErrorCode)ex.getCustomCode();
                new_status.start_time = start_ts;
                new_status.status = (mk_status)status;
                new_status.priv = userdata;
                strong_self->_device_cb((mgw_handler_t*)obj, MK_EVENT_SET_STREAM_STATUS, (void*)&new_status);
            }
        };

        auto on_register = [weak_self, on_status_changed, pusher_info](const MediaSource::Ptr &src){
            auto strong_self = weak_self.lock();
            if (!strong_self) { return; }

            if (src) {
                strong_self->_device_helper->addPusher(pusher_info.name, pusher_info.remote,
                                pusher_info.url, src, on_status_changed, pusher_info.netif,
                                pusher_info.mtu, pusher_info.userdata);
            } else {
                //超时仍找不到源，推流失败
                on_status_changed(pusher_info.name, ChannelStatus_Idle, 0,
                    SockException(Err_timeout, "Wait source timeout", -1), pusher_info.userdata);
            }
        };

        MediaSource::findAsync(media_info, _ws_client, on_register);
    } else {
        //请求mgw-server服务器代理推流
        if (!_ws_client) {
            WarnL << "Websocket client invalid!";
            return -2;
        }
        //切换到自己的线程再发送请求消息
        _poller->async([weak_self, pusher_info, src_name](){
            auto strong_self = weak_self.lock();
            if (!strong_self) { return; }

            //请求服务器代理推流，本地只创建一个镜像pusher用来保存信息
            strong_self->_device_helper->addPusher(pusher_info.name, pusher_info.remote,
                            pusher_info.url, nullptr, nullptr, "", 0, pusher_info.userdata);

            //
            strong_self->_ws_client->sendStartProxyPush(pusher_info.channel, pusher_info.url, getSourceChn(src_name));
        }, false);
    }
    return 0;
}

void DeviceHandle::releasePusher(bool remote, int channel) {
    _device_helper->releasePusher(getOutputName(remote, channel));
    if (remote && _ws_client) {
        _ws_client->sendStopProxyPush(channel);
    }
}

bool DeviceHandle::hasPusher(bool remote, int channel) {
    return _device_helper->hasPusher(getOutputName(remote, channel));
}

bool DeviceHandle::hasTunnelPusher() {
    return _device_helper->hasPusher(TUNNEL_PUSHER);
}

void DeviceHandle::releaseTunnelPusher() {
    _device_helper->releasePusher(TUNNEL_PUSHER);
}

//player
int DeviceHandle::addPlayer(player_attr info) {
    auto on_play_status = [&, info](const string &name, ChannelStatus status, Time_t start_ts, const SockException &ex){
        if (!info.status_cb) {
            return;
        }

        status_info sta;
        sta.channel = info.channel;
        sta.error = ex.getCustomCode();
        sta.priv = NULL;
        sta.remote = name.find('R') != string::npos;
        sta.start_time = start_ts;
        sta.status = (mk_status)status;
        info.status_cb(info.channel, sta);
    };

    auto src_name = getSourceName(info.remote, info.channel, _device_helper->sn());
    if (!info.data_cb) {
        _device_helper->addPlayer(src_name, info.url, on_play_status, nullptr, info.netif, info.mtu);
    } else {
        auto on_play_data = [&, info](CodecId id, const char *data, uint32_t size, uint64_t dts, uint64_t pts, bool keyframe) {
            if (!info.data_cb) {
                return;
            }
            mk_frame_t frame;
            switch (id) {
                case CodecH264: frame.id = MK_CODEC_ID_H264; break;
                case CodecH265: frame.id = MK_CODEC_ID_H265; break;
                case CodecAAC: frame.id = MK_CODEC_ID_AAC; break;
                default: { WarnL << "Not support codec: " << id; return; }
            }
            frame.data = data;
            frame.size = size;
            frame.keyframe = keyframe;
            frame.dts = dts;
            frame.pts = pts;
            info.data_cb(info.channel, frame);
        };
        _device_helper->addPlayer(src_name, info.url, on_play_status, on_play_data, info.netif, info.mtu);
    }
    return 0;
}

void DeviceHandle::releasePlayer(bool remote, int channel) {
    _device_helper->releasePlayer(getSourceName(remote, channel, _device_helper->sn()));
}

//play service
void DeviceHandle::setPlayService(play_service_attr *attr) {

}

void DeviceHandle::getPlayService(play_service_attr *attr) {

}

///////////////////////////////////////////////////////////////////////////

mgw_handler_t *mgw_create_device(mgw_context *ctx) {
    assert(ctx);
    DeviceHandle::Ptr *obj(new DeviceHandle::Ptr(new DeviceHandle()));
    bool result = obj->get()->startup(ctx);
    if (!result) {
        ErrorL << "Start mgw-zlm failed!";
        mgw_release_device(obj);
        return NULL;
    }
    return (mgw_handler_t*)obj;
}

void mgw_release_device(mgw_handler_t *h) {
    assert(h);
    DeviceHandle::Ptr *obj = (DeviceHandle::Ptr *) h;
    delete obj;
}

///////////////////////////////////////////////////////////////////////////
int mgw_connect2server(mgw_handler_t *h, const conn_info *info) {
    assert(h && info);
    ((DeviceHandle::Ptr*)h)->get()->connectServer(info);
    return 0;
}

void mgw_disconnect4server(mgw_handler_t *h) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->disconnect();
}

bool mgw_server_available(mgw_handler_t *h) {
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->messageAvailable();
}

///////////////////////////////////////////////////////////////////////////
int mgw_add_source(mgw_handler_t *h, source_info *info) {
    assert(h && info);
    bool ret = ((DeviceHandle::Ptr*)h)->get()->addRawSource(info);
    return ret ? 0 : -1;
}

int mgw_input_packet(mgw_handler_t *h, uint32_t channel, mk_frame_t frame) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->inputFrame(channel, frame);
    return 0;
}

void mgw_release_source(mgw_handler_t *h, bool local, uint32_t channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->releaseRawSource(local, channel);
}

void mgw_update_meta(mgw_handler_t *h, uint32_t channel, stream_meta *info) {
    assert(h && info);
    ((DeviceHandle::Ptr*)h)->get()->updateMeta(true, channel, info);
}

bool mgw_has_source(mgw_handler_t *h, bool local, uint32_t channel) {
    // auto src = MediaSource::find(DEFAULT_VHOST, "live", getSourceName(!local, channel));
    // return !!src;
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->hasSource(local, channel);
}

void mgw_start_recorder(mgw_handler_t *h, bool local, uint32_t channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->setupRecord(local, channel, true);
}

void mgw_stop_recorder(mgw_handler_t *h, bool local, uint32_t channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->setupRecord(local, channel, false);
}

///////////////////////////////////////////////////////////////////////////
int mgw_add_pusher(mgw_handler_t *h, pusher_info *info) {
    assert(h && info);
    return ((DeviceHandle::Ptr*)h)->get()->addPusher(info);
}

void mgw_release_pusher(mgw_handler_t *h, bool remote, uint32_t chn) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->releasePusher(remote, chn);
}

bool mgw_has_pusher(mgw_handler_t *h, bool remote, uint32_t chn) {
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->hasPusher(remote, chn);
}

bool mgw_has_tunnel_pusher(mgw_handler_t *h) {
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->hasTunnelPusher();
}

void mgw_release_tunnel_pusher(mgw_handler_t *h) {
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->releaseTunnelPusher();
}

///////////////////////////////////////////////////////////////////////////
void mgw_set_play_service(mgw_handler_t *h, play_service_attr *attr) {

}

void mgw_get_play_service(mgw_handler_t *h, play_service_attr *attr) {

}

///////////////////////////////////////////////////////////////////////////
void mgw_add_player(mgw_handler_t *h, player_attr attr) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->addPlayer(attr);
}

void mgw_release_player(mgw_handler_t *h, bool remote, int channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->releasePlayer(remote, channel);
}