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
using namespace MGW;

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
            int pos = 0;
            if (info->key[0] == '/') {
                pos = 1;
            }
            url += string("/") + (info->key+pos);
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

    bool initVideo(track_info info);
    bool initAudio(track_info info);

    string getStreamId() { return _stream_id; }

private:
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
    bool hasRawVideo(uint32_t channel);
    bool hasRawAudio(uint32_t channel);
    bool addRawVideo(uint32_t channel, track_info info);
    bool addRawAudio(uint32_t channel, track_info info);
    void releaseRawSource(bool local, int channel);
    void inputFrame(uint32_t channel, const mk_frame_t &frame);
    void updateMeta(bool local, int channel, track_info info);
    bool setupRecord(bool local, input_type_t it, int channel, bool start = true);

    //pusher
    int addPusher(pusher_info *info);
    void releasePusher(bool remote, int channel);
    bool hasPusher(bool remote, int channel);
    bool hasTunnelPusher();
    void releaseTunnelPusher();

    //player
    int addPlayer(player_attr info);
    void releasePlayer(bool remote, input_type_t it, int channel);

    //play service
    void setPlayService(play_service_attr *attr);
    void getPlayService(play_service_attr *attr);

private:
    void setDeviceCallback();

    template<typename T>
    void startTcpServer(uint16_t port);
    void setService(uint16_t port, const string &schema, bool stop, bool stop_all);

private:
    bool                _have_session_rsp = false;
    mk_codec_id         _video_eid = MK_CODEC_ID_NONE;
    //线程管理
    EventPoller::Ptr    _poller = nullptr;
    //设备管理
    DeviceHelper::Ptr   _device_helper = nullptr;
    //tcp 服务器，rtsp，rtmp，http
    mutex               _svr_mutex;
    unordered_map<uint16_t/*port*/, pair<TcpServer::Ptr, bool/*access*/>> _svr_map;
    //
    //媒体源管理
    mutex               _src_mutex;
    unordered_map<int, UcastSourceHelper::Ptr>  _sources;
    /*----------------------------------------------*/
    mgw_callback        _device_cb = NULL;
    /*-----------------ws信息------------------------*/
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
                : _stream_id(id) {
    _poller = poller ? poller : EventPollerPool::Instance().getPoller();
    _poller->sync([&]() { _channel = make_shared<DevChannel>(vhost, app, id); });
}
UcastSourceHelper::~UcastSourceHelper() {

}

bool UcastSourceHelper::initVideo(track_info info) {
    VideoInfo vinfo;
    if (info.id == MK_CODEC_ID_H264) {
        vinfo.codecId = CodecH264;
    } else if (info.id == MK_CODEC_ID_H265) {
        vinfo.codecId = CodecH265;
    } else {
        WarnL << "Not support this video codecid: " << info.id;
        return false;
    }

    vinfo.iFrameRate = info.video.fps;
    vinfo.iWidth = info.video.width;
    vinfo.iHeight = info.video.height;
    vinfo.iBitRate = info.video.vkbps*1000;
    DebugL << "Add video track id: " << vinfo.codecId << " fps: " <<
                vinfo.iFrameRate << " width: " << vinfo.iWidth << " height: " << 
                vinfo.iHeight << " bitrate: " << vinfo.iBitRate;

    return _channel->initVideo(vinfo);
}

bool UcastSourceHelper::initAudio(track_info info) {
    //加载音频
    if (info.id != MK_CODEC_ID_AAC) {
        WarnL << "Not support this audio codecid: " << info.id;
        return false;
    }

    AudioInfo ainfo;
    ainfo.codecId = CodecAAC;
    ainfo.iSampleRate = info.audio.samplerate;
    ainfo.iChannel = info.audio.channels;
    ainfo.iSampleBit = info.audio.samplesize;
    DebugL << "Add audio track id: " << ainfo.codecId << " chn: " <<
            ainfo.iChannel << " sr: " << ainfo.iSampleRate << " sb: " << ainfo.iSampleBit;
    _channel->initAudio(ainfo);
    return true;
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
    //推流或者拉流找不到流，可以通知启动设备编码器，按需编码或者按需拉流
    _device_helper->setOnNotFoundStream([&](const std::string &url) {
        //切换回自己的线程再执行
        _poller->async([&, url](){
            if (_device_cb) {
                int chn = getSourceChn(url);
                //如果能找得到通道号，说明是设备流
                if (-1 != chn) {
                    _device_cb((mgw_handler_t*)obj, MK_EVENT_SET_START_VIDEO, &chn);
                } else {
                    //TODO: 网络流，开始去拉流
                }
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
    _device_helper->setOnNoReader([&](const string &id){
        DebugL << "On no reader";
        if (_device_cb) {
            int chn = getSourceChn(id);
            _device_cb((mgw_handler_t*)obj, MK_EVENT_SET_STOP_VIDEO, (void*)&chn);
        }
    });
    //推拉流鉴权处理，设备上的推拉流不做鉴权，直接通过
    _device_helper->setOnAuthen([&](const string &url)->bool{
        bool access = false;
        if (url.substr(0, 4) == "rtmp") {
            uint16_t port = mINI::Instance()[Rtmp::kPort];
            lock_guard<mutex> lock(_svr_mutex);
            auto it = _svr_map.find(port);
            if (it != _svr_map.end() && it->second.second) {
                access = true;
            }
        } else if (url.substr(0, 5) == "rtmps") {
            uint16_t port = mINI::Instance()[Rtmp::kSSLPort];
            lock_guard<mutex> lock(_svr_mutex);
            auto it = _svr_map.find(port);
            if (it != _svr_map.end() && it->second.second) {
                access = true;
            }
        } else if (url.substr(0, 4) == "rtsp") {
            uint16_t port = mINI::Instance()[Rtsp::kPort];
            lock_guard<mutex> lock(_svr_mutex);
            auto it = _svr_map.find(port);
            if (it != _svr_map.end() && it->second.second) {
                access = true;
            }
        } else if (url.substr(0, 5) == "rtsps") {
            uint16_t port = mINI::Instance()[Rtsp::kSSLPort];
            lock_guard<mutex> lock(_svr_mutex);
            auto it = _svr_map.find(port);
            if (it != _svr_map.end() && it->second.second) {
                access = true;
            }
        } else if (url.substr(0, 4) == "http") {
            uint16_t port = mINI::Instance()[Http::kPort];
            lock_guard<mutex> lock(_svr_mutex);
            auto it = _svr_map.find(port);
            if (it != _svr_map.end() && it->second.second) {
                access = true;
            }
        } else if (url.substr(0, 5) == "https") {
            uint16_t port = mINI::Instance()[Http::kSSLPort];
            lock_guard<mutex> lock(_svr_mutex);
            auto it = _svr_map.find(port);
            if (it != _svr_map.end() && it->second.second) {
                access = true;
            }
        }

        return access;
    });
}

template<typename T>
void DeviceHandle::startTcpServer(uint16_t port) {
    auto svr = make_shared<TcpServer>();
    if (port && svr) {
        svr->start<T>(port);
        lock_guard<mutex> lock(_svr_mutex);
        _svr_map[port] = make_pair(svr, true);
    }
}

void DeviceHandle::setService(uint16_t port, const string &schema, bool stop, bool stop_all) {
    auto it = _svr_map.find(port);
    if (it != _svr_map.end()) {
        lock_guard<mutex> lock(_svr_mutex);
        if (stop && it->second.second) {
            it->second.second = false;
            if (stop_all) {
                _svr_map.erase(port);
            }
        } else if (!stop && !it->second.second) {
            it->second.second = true;
        }
    } else {
        //map中没有这个服务，要求打开这个服务
        if (!stop) {
            if (0 == schema.compare("rtmp")) {
                startTcpServer<RtmpSession>(port);
            } else if (0 == schema.compare("rtmps")) {
                startTcpServer<RtmpSessionWithSSL>(port);
            } else if (0 == schema.compare("rtsp")) {
                startTcpServer<RtspSession>(port);
            } else if (0 == schema.compare("rtsps")) {
                startTcpServer<RtspSessionWithSSL>(port);
            } else if (0 == schema.compare("http")) {
                startTcpServer<HttpSession>(port);
            } else if (0 == schema.compare("https")) {
                startTcpServer<HttpsSession>(port);
            }
        }
    }
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
        startTcpServer<RtspSession>(rtspPort);          //rtsp服务器，端口默认554
        startTcpServer<RtspSessionWithSSL>(rtspsPort);  //rtsps服务器，端口默认332
        startTcpServer<RtmpSession>(rtmpPort);          //rtmp服务器，端口默认1935
        startTcpServer<RtmpSessionWithSSL>(rtmpsPort);  //rtmps服务器，端口默认19350
        startTcpServer<HttpSession>(httpPort);          //http服务器，端口默认80
        startTcpServer<HttpsSession>(httpsPort);        //https服务器，端口默认443
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

bool DeviceHandle::hasRawVideo(uint32_t channel) {
    lock_guard<mutex> lock(_src_mutex);
    auto source = _sources[channel];
    if (!source) {
        return false;
    }
    return source->getChannel()->haveVideo();
}

bool DeviceHandle::hasRawAudio(uint32_t channel) {
    lock_guard<mutex> lock(_src_mutex);
    auto source = _sources[channel];
    if (!source) {
        return false;
    }
    return source->getChannel()->getTrack(TrackAudio) ||
           source->getChannel()->getTrack(TrackAudio, false);
}

void DeviceHandle::releaseRawSource(bool local, int channel) {

}

bool DeviceHandle::setupRecord(bool local, input_type_t it, int channel, bool start) {
    if (local) {
        string streamid = getStreamId(_device_helper->sn(), Location_Dev, (InputType)it, channel);
        auto src = MediaSource::find(DEFAULT_VHOST, "live", streamid);
        if (!src) {
            WarnL << "Not found the source:" << streamid;
            return false;
        }
        return src->setupRecord(Recorder::type_mp4, start, "./record/", 60*60);
    } else {
        //TODO: 应该发送消息到mgw-server服务器开启/关闭录像
        return false;
    }
}

void DeviceHandle::inputFrame(uint32_t channel, const mk_frame_t &frame) {
    UcastSourceHelper::Ptr source = nullptr;
    {
        lock_guard<mutex> lock(_src_mutex);
        source = _sources[channel];
        if (!source) {
            string id = getStreamId(_device_helper->sn(), Location_Dev, InputType_Phy, channel);
            source = make_shared<UcastSourceHelper>(DEFAULT_VHOST, "live", id, _poller);
            _sources.emplace(channel, source);
        }
    }

    auto chn  = source->getChannel();
    if (frame.id != MK_CODEC_ID_AAC && frame.id != _video_eid) {
        WarnL << "frame eid: " << frame.id << " old eid:" << _video_eid;
        return;
    }

    switch (frame.id) {
        case MK_CODEC_ID_H264:
        case MK_CODEC_ID_H265: {
            if (!chn->getTrack(TrackVideo, false) && !chn->getTrack(TrackVideo, true) && _device_cb) {
                track_info meta = {};
                meta.id = frame.id;
                _device_cb((mgw_handler_t*)obj, MK_EVENT_GET_STREAM_META, (void*)&meta);
                if (!source->initVideo(meta)) {
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
                track_info meta = {};
                _device_cb((mgw_handler_t*)obj, MK_EVENT_GET_STREAM_META, (void*)&meta);
                if (!source->initAudio(meta)) {
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

void DeviceHandle::updateMeta(bool local, int channel, track_info info) {
    lock_guard<mutex> lock(_src_mutex);
    auto source = _sources[channel];
    if (!source) {
        return;
    }

    auto chn = source->getChannel();
    if ((info.id == MK_CODEC_ID_H264 ||
         info.id == MK_CODEC_ID_H265) && info.id != _video_eid) {
        _video_eid = info.id;
        //reset 所有的track，原来所有的输出就会失效，所以在还有消费者的时候，不允许更改编码器类型
        chn->resetTracks();
        //需要重新初始化
        source->initVideo(info);
    }
}

//pusher
int DeviceHandle::addPusher(pusher_info *info) {
    string streamid = getStreamId(_device_helper->sn(),
                        info->src_remote ? Location_Svr : Location_Dev,
                        (InputType)info->src_type, info->src_chn);

    PusherInfo pusher_info(info);
    weak_ptr<DeviceHandle> weak_self = shared_from_this();

    if (!info->remote) {
        ostringstream oss;
        //注意，如果子串开头就是字符串开头，不要填入
        oss << FindField(info->url, NULL, "://") << "://" << DEFAULT_VHOST << "/live/" << streamid;
        MediaInfo media_info(oss.str());

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
        _poller->async([weak_self, pusher_info, streamid](){
            auto strong_self = weak_self.lock();
            if (!strong_self) { return; }

            //请求服务器代理推流，本地只创建一个镜像pusher用来保存信息
            strong_self->_device_helper->addPusher(pusher_info.name, pusher_info.remote,
                            pusher_info.url, nullptr, nullptr, "", 0, pusher_info.userdata);

            //
            strong_self->_ws_client->sendStartProxyPush(pusher_info.channel,
                                        pusher_info.url, getSourceChn(streamid));
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

static mk_codec_id get_codec_id(CodecId id) {
    switch (id) {
        case CodecH264: return MK_CODEC_ID_H264;
        case CodecH265: return MK_CODEC_ID_H265;
        case CodecAAC: return MK_CODEC_ID_AAC;
        default: { WarnL << "Not support codec: " << id; break; }
    }
    return MK_CODEC_ID_NONE;
}

//player
int DeviceHandle::addPlayer(player_attr info) {
    string streamid = getStreamId(_device_helper->sn(),
                        info.remote ? Location_Svr : Location_Dev,
                        (InputType)info.it, info.channel);

    if (!info.remote) {
        auto on_play_status = [&, info](const string &name, ChannelStatus status,
                                        Time_t start_ts, const SockException &ex){
            if (!info.status_cb) {
                return;
            }
            status_info sta;
            sta.channel = info.channel;
            sta.error = ex.getCustomCode();
            sta.priv = NULL;
            sta.remote = name.find("_SVR_") != string::npos;
            sta.start_time = start_ts;
            sta.status = (mk_status)status;
            info.status_cb(info.channel, sta);
        };
        PlayHelper::onData on_play_data = nullptr;
        PlayHelper::onMeta on_play_meta = nullptr;
        if (info.data_cb) {
            on_play_data = [&, info](CodecId id, const char *data,
                    uint32_t size, uint64_t dts, uint64_t pts, bool keyframe) {
                mk_frame_t frame;
                frame.id = get_codec_id(id);
                frame.data = data;
                frame.size = size;
                frame.keyframe = keyframe;
                frame.dts = dts;
                frame.pts = pts;
                info.data_cb(info.channel, frame);
            };
        }
        if (info.meta_cb) {
            on_play_meta = [&, info](CodecId id, uint16_t width_chns,
                    uint16_t height_samplerate, uint16_t fps_samplesize, uint16_t a_v_kbps) {
                track_info meta = {};
                if (id != CodecAAC) {
                    meta.id = get_codec_id(id);
                    meta.video.width = width_chns;
                    meta.video.height = height_samplerate;
                    meta.video.fps = fps_samplesize;
                    meta.video.vkbps = a_v_kbps;
                } else {
                    meta.id = MK_CODEC_ID_AAC;
                    meta.audio.channels = width_chns;
                    meta.audio.samplerate = height_samplerate;
                    meta.audio.samplesize = fps_samplesize;
                    meta.audio.akbps = a_v_kbps;
                }
                info.meta_cb(info.channel, INPUT_TYPE_PLA, meta);
            };
        }
        _device_helper->addPlayer(streamid, info.remote, info.url,
                        on_play_status, on_play_data, on_play_meta,
                        info.netif?info.netif:"", info.mtu);
    } else {
        //TODO: 请求服务器代理拉流
    }
    return 0;
}

void DeviceHandle::releasePlayer(bool remote, input_type_t it, int channel) {
    string streamid = getStreamId(_device_helper->sn(),
                        remote ? Location_Svr : Location_Dev,
                        (InputType)it, channel);
    if (!remote) {
        _device_helper->releasePlayer(streamid);
    } else {
        //TODO: 停止服务器代理拉流
    }
}

static uint16_t get_port(const string &schema) {
    uint16_t port = 0;
    if (0 == schema.compare("rtmp")) {
        port = mINI::Instance()[Rtmp::kPort];
    } else if (0 == schema.compare("rtmps")) {
        port = mINI::Instance()[Rtmp::kSSLPort];
    } else if (0 == schema.compare("rtsp")) {
        port = mINI::Instance()[Rtsp::kPort];
    } else if (0 == schema.compare("rtsps")) {
        port = mINI::Instance()[Rtsp::kSSLPort];
    } else if (0 == schema.compare("http")) {
        port = mINI::Instance()[Http::kPort];
    } else if (0 == schema.compare("https")) {
        port = mINI::Instance()[Http::kSSLPort];
    }
    return port;
}

//play service
void DeviceHandle::setPlayService(play_service_attr *attr) {
    if (!attr->schema) {
        return;
    }
    string schema = attr->schema;
    bool stop = attr->stop;
    bool stop_all = attr->stop_all;
    bool local = attr->local_service;
    weak_ptr<DeviceHandle> weak_self = shared_from_this();

    _poller->async([weak_self, local, schema, stop, stop_all](){
        auto strong_self = weak_self.lock();
        if (!strong_self) { return; }

        if (local) {
            uint16_t port = get_port(schema);
            strong_self->setService(port, schema, stop, stop_all);
        } else {
            //发送消息设置mgw-server服务
        }
    });
}

void DeviceHandle::getPlayService(play_service_attr *attr) {
    if (!attr->schema) {
        return;
    }

    if (attr->local_service) {
        uint16_t port = get_port(attr->schema);
        lock_guard<mutex> lock(_svr_mutex);
        auto it = _svr_map.find(port);
        if (it == _svr_map.end()) {
            attr->stop = true;
            attr->stop_all = true;
            return;
        }
        attr->stop_all = false;
        attr->stop = it->second.second;
        {
            lock_guard<mutex> lock(_src_mutex);
            auto source = _sources[attr->src_chn];
            if (source) {
                attr->players = source->getChannel()->totalReaderCount();
            }
        }

        stringstream oss;
        oss << attr->schema << "://" << mINI::Instance()[Mgw::kOutHostIP] << "/live/";
        oss << getStreamId(_device_helper->sn(), Location_Dev, InputType_Phy, 0);
        attr->play_url = strdup(oss.str().c_str());
    } else {
        //返回mgw-server同步来的信息
    }
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
int mgw_input_packet(mgw_handler_t *h, uint32_t channel, mk_frame_t frame) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->inputFrame(channel, frame);
    return 0;
}

void mgw_release_source(mgw_handler_t *h, bool local, uint32_t channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->releaseRawSource(local, channel);
}

void mgw_update_meta(mgw_handler_t *h, uint32_t channel, track_info info) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->updateMeta(true, channel, info);
}

bool mgw_have_raw_video(mgw_handler_t *h, uint32_t channel) {
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->hasRawVideo(channel);
}

bool mgw_have_raw_audio(mgw_handler_t *h, uint32_t channel) {
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->hasRawAudio(channel);
}

//-----------------------------------------------------------------------------------------------
//record
void mgw_start_recorder(mgw_handler_t *h, bool local, input_type_t it, uint32_t channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->setupRecord(local, it, channel, true);
}

void mgw_stop_recorder(mgw_handler_t *h, bool local, input_type_t it, uint32_t channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->setupRecord(local, it, channel, false);
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
    assert(h && attr);
    ((DeviceHandle::Ptr*)h)->get()->setPlayService(attr);
}

void mgw_get_play_service(mgw_handler_t *h, play_service_attr *attr) {
    assert(h && attr);
    ((DeviceHandle::Ptr*)h)->get()->getPlayService(attr);
}

///////////////////////////////////////////////////////////////////////////
void mgw_add_player(mgw_handler_t *h, player_attr attr) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->addPlayer(attr);
}

void mgw_release_player(mgw_handler_t *h, bool remote, input_type_t it, int channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->releasePlayer(remote, it, channel);
}