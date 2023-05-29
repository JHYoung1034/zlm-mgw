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
namespace mediakit {

//用于处理输入源，监听输入源事件，参考mk_media.cpp
class UcastSourceHelper : public MediaSourceEvent, public enable_shared_from_this<UcastSourceHelper> {
public:
    using Ptr = shared_ptr<UcastSourceHelper>;
    using onMeta = function<void(uint32_t/*channel*/, stream_meta* /*info*/)>;
    using onFrame = function<void(uint32_t /*channel*/, mgw_packet* /*pkt*/)>;

    UcastSourceHelper(const string &vhost, const string &app, const string &id, const EventPoller::Ptr &poller);
    ~UcastSourceHelper();

    DevChannel::Ptr &getChannel() { return _channel; }

    //添加音视频，完成后调用addTrackCompleted
    void initSource(source_info *src_info);
    void initLocalSource(stream_meta *info);
    void initPlaySource(play_info *info);

private:
    bool                _local_source;
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
    bool addRawSource(source_info *info);
    void releaseRawSource(bool local, int channel);
    void inputFrame(uint32_t channel, mgw_packet *pkt);
    void updateMeta(bool local, int channel, stream_meta *info);
    bool setupRecord(bool local, int channel, bool start = true);

    //pusher
    int addPusher(pusher_info *info);
    void releasePusher(bool remote, int channel);
    bool hasPusher(bool remote, int channel);

    //player
    int addPlayer(player_attr *info);
    void releasePlayer(bool remote, int channel);

    //play service
    void setPlayService(play_service_attr *attr);
    void getPlayService(play_service_attr *attr);

private:
    void setDeviceCallback();

private:
    bool                _have_session_rsp = false;

    EncoderId           _video_eid = EncoderId_None;

    EventPoller::Ptr    _poller = nullptr;
    DeviceHelper::Ptr   _device_helper = nullptr;
    clientPtr           _ws_client = nullptr;
    unordered_map<uint16_t, TcpServer::Ptr> _svr_map;

    UcastSourceHelper::Ptr _source_helper = nullptr;

    /*----------------------------------------------*/
    mgw_callback        _device_cb = NULL;
};

/**-------------------------------------------------*/
//全局设备管理器
static DeviceHandle::Ptr *obj = nullptr;

//-------------------------------------------------------------------------------
UcastSourceHelper::UcastSourceHelper(const string &vhost, const string &app,
                const string &id, const EventPoller::Ptr &poller) : _local_source(true) {
    _poller = poller ? poller : EventPollerPool::Instance().getPoller();
    _poller->sync([&]() { _channel = make_shared<DevChannel>(vhost, app, id); });
}
UcastSourceHelper::~UcastSourceHelper() {

}

void UcastSourceHelper::initLocalSource(stream_meta *info) {
    _local_source = true;

    VideoInfo vinfo;
    if (info->video_id == EncoderId_H264) {
        vinfo.codecId = CodecH264;
    } else if (info->video_id == EncoderId_H265) {
        vinfo.codecId = CodecH265;
    }
    vinfo.iFrameRate = info->fps;
    vinfo.iWidth = info->width;
    vinfo.iHeight = info->height;
    vinfo.iBitRate = info->vkbps*1000;
    _channel->initVideo(vinfo);

    //加载音频
    if (info->audio_id == EncoderId_AAC) {
        AudioInfo ainfo;
        ainfo.codecId = CodecAAC;
        ainfo.iSampleRate = info->samplerate;
        ainfo.iChannel = info->channels;
        ainfo.iSampleBit = info->samplesize;
        _channel->initAudio(ainfo);
    } else {
        WarnL << "Not support this audio codecid: " << info->audio_id;
    }
    //完成音视频加载
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
            _device_cb((handler_t*)obj, (void*)&info);
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
            _device_cb((handler_t*)obj, (void*)&attrs);
        }
        //收到会话回复才会调用这个回调，说明会话请求已经被回复了，可以进行后面的操作了
        _have_session_rsp = true;
    });
    //观看人数发生变化，应该注意判断是 设备本地的服务 还是 mgw-server的服务
    _device_helper->setOnPlayersChange([&](bool local, int players) {
        DebugL << "on players changed: " << players;
    });
    //推流或者拉流找不到流，可以通知启动设备编码器，按需编码
    _device_helper->setOnNotFoundStream([&](const std::string &url) {
        DebugL << "on not found stream: " << url;

    });
    //这个回调很重要，定时回调通知同步设备推流的状态
    _device_helper->setOnStatusChanged([&](ChannelType chn_type, ChannelId chn, ChannelStatus status, ErrorCode err, Time_t start_ts) {
        if (_device_cb &&
            (ChannelType_Output == chn_type ||
             ChannelType_ProxyOutput==chn_type)) {
            pusher_status status_info;
            status_info.remote = ChannelType_ProxyOutput==chn_type;
            status_info.channel = chn;
            status_info.status = status;
            status_info.error = err;
            status_info.start_time = start_ts;
            status_info.priv = NULL;
            
            _device_cb((handler_t*)obj, (void*)&status_info);
        }
    });
    //流已经没有消费者了，此时可以通知编码器停止编码
    _device_helper->setOnNoReader([&](){
        DebugL << "On no reader";
    });
}

bool DeviceHandle::startup(mgw_context *ctx) {
    assert(ctx);

    int level = ctx->log_level;
    string log_path(ctx->log_path);
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
                            ctx->token, ctx->stream_attr.max_bitrate, ctx->stream_attr.max_4kbitrate,
                            ctx->stream_attr.max_pushers, ctx->stream_attr.max_players);
    device->loadConfig(cfg);
    setDeviceCallback();

    //初始化websocket客户端
    _ws_client = make_shared<WebSocketClient<MessageClient, WebSocketHeader::BINARY> >(_poller, Entity_Device, _device_helper);

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
    //设置网络接口
    if (info->netif) {
        _ws_client->setNetAdapter(info->netif);
    }
    if (info->ka_sec) {
        _ws_client->setKaSec(info->ka_sec);
    }

    //连接到mgw-server
    string sub = FindField(info->url, "://", "/");
    string port = FindField(sub.data(), ":", "");
    _ws_client->startConnect(info->url, atoi(port.data()));
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
bool DeviceHandle::addRawSource(source_info *info) {
    if (!_source_helper) {
        _source_helper = make_shared<UcastSourceHelper>(DEFAULT_VHOST, "live",
                            getSourceName(!info->local, info->channel), _poller);
    }

    _source_helper->initSource(info);
    if (info->local) {
        _video_eid = info->local_src.video_id;
    }

    return true;
}

void DeviceHandle::releaseRawSource(bool local, int channel) {

}

bool DeviceHandle::setupRecord(bool local, int channel, bool start) {
    string id = getSourceName(false, channel);
    auto src = MediaSource::find(DEFAULT_VHOST, "live", id);
    if (!src) {
        WarnL << "Not found the source:" << id;
        return false;
    }
    return src->setupRecord(Recorder::type_mp4, start, "./record/", 60*60);
}

void DeviceHandle::inputFrame(uint32_t channel, mgw_packet *pkt) {
    auto chn  = _source_helper->getChannel();
    if (!chn) {
        return;
    }
    if (pkt->type == EncoderType_Video && pkt->eid != _video_eid) {
        WarnL << "frame eid: " << pkt->eid << " Not equal video_eid: " << _video_eid;
        return;
    }

    switch (pkt->eid) {
        case EncoderId_H264: {
            chn->inputH264((const char *)pkt->data, pkt->size, pkt->dts, pkt->pts);
            break;
        }
        case EncoderId_H265: {
            chn->inputH265((const char *)pkt->data, pkt->size, pkt->dts, pkt->pts);
            break;
        }
        case EncoderId_AAC: {
            chn->inputAAC((const char *)pkt->data+7, pkt->size-7, pkt->dts, (const char *)pkt->data);
            break;
        }
        default: {
            WarnL << "Not support this encoder id: " << pkt->eid;
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
    auto source = MediaSource::find(DEFAULT_VHOST, "live", getSourceName(info->src_remote, info->src_chn));
    if (!source) {
        WarnL << "找不到源";
        return -1;
    }

    auto on_status_changed = [&](const string &name, ChannelStatus status, Time_t start_ts, const SockException &ex){
        if (_device_cb) {
            pusher_status new_status;
            new_status.channel = getOutputChn(name);
            new_status.remote = name.find('R') != string::npos;
            new_status.error = (ErrorCode)ex.getCustomCode();
            new_status.start_time = start_ts;
            new_status.status = status;
            new_status.remote = NULL;
            _device_cb((handler_t*)obj, (void*)&new_status);
        }
    };

    auto output_name = getOutputName(info->remote, info->pusher_chn);
    _device_helper->addPusher(output_name, info->url, source,
                    on_status_changed, info->bind_netif, info->bind_mtu);
    return 0;
}

void DeviceHandle::releasePusher(bool remote, int channel) {
    _device_helper->releasePusher(getOutputName(remote, channel));
}

bool DeviceHandle::hasPusher(bool remote, int channel) {
    return _device_helper->hasPusher(getOutputName(remote, channel));
}

//player
int DeviceHandle::addPlayer(player_attr *info) {

}

void DeviceHandle::releasePlayer(bool remote, int channel) {

}

//play service
void DeviceHandle::setPlayService(play_service_attr *attr) {

}

void DeviceHandle::getPlayService(play_service_attr *attr) {

}

///////////////////////////////////////////////////////////////////////////

handler_t *mgw_create_device(mgw_context *ctx) {
    assert(ctx);
    DeviceHandle::Ptr *obj(new DeviceHandle::Ptr(new DeviceHandle()));
    bool result = obj->get()->startup(ctx);
    if (!result) {
        ErrorL << "Start mgw-zlm failed!";
        mgw_release_device(obj);
        return NULL;
    }
    return (handler_t*)obj;
}

void mgw_release_device(handler_t *h) {
    assert(h);
    DeviceHandle::Ptr *obj = (DeviceHandle::Ptr *) h;
    delete obj;
}

///////////////////////////////////////////////////////////////////////////
int mgw_connect2server(handler_t *h, const conn_info *info) {
    assert(h && info);
    ((DeviceHandle::Ptr*)h)->get()->connectServer(info);
    return 0;
}

void mgw_disconnect4server(handler_t *h) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->disconnect();
}

bool mgw_server_available(handler_t *h) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->messageAvailable();
}

///////////////////////////////////////////////////////////////////////////
int mgw_add_source(handler_t *h, source_info *info) {
    assert(h && info);
    bool ret = ((DeviceHandle::Ptr*)h)->get()->addRawSource(info);
    return ret ? 0 : -1;
}

int mgw_input_packet(handler_t *h, uint32_t channel, mgw_packet *pkt) {
    assert(h && pkt);
    ((DeviceHandle::Ptr*)h)->get()->inputFrame(channel, pkt);
    return 0;
}

void mgw_release_source(handler_t *h, bool local, uint32_t channel) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->releaseRawSource(local, channel);
}

void mgw_update_meta(handler_t *h, uint32_t channel, stream_meta *info) {
    assert(h && info);
    ((DeviceHandle::Ptr*)h)->get()->updateMeta(true, channel, info);
}

bool mgw_has_source(handler_t *h, uint32_t channel) {
    auto src = MediaSource::find(DEFAULT_VHOST, "live", getSourceName(false, channel));
    return !!src;
}

void mgw_start_recorder(handler_t *h, uint32_t channel, enum InputType type) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->setupRecord(true, channel, true);
}

void mgw_stop_recorder(handler_t *h, uint32_t channel, enum InputType type) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->setupRecord(true, channel, false);
}

///////////////////////////////////////////////////////////////////////////
int mgw_add_pusher(handler_t *h, pusher_info *info) {
    assert(h && info);
    return ((DeviceHandle::Ptr*)h)->get()->addPusher(info);
}

void mgw_release_pusher(handler_t *h, bool remote, uint32_t chn) {
    assert(h);
    ((DeviceHandle::Ptr*)h)->get()->releasePusher(remote, chn);
}

bool mgw_has_pusher(handler_t *h, bool remote, uint32_t chn) {
    assert(h);
    return ((DeviceHandle::Ptr*)h)->get()->hasPusher(remote, chn);
}

///////////////////////////////////////////////////////////////////////////
void mgw_set_play_service(handler_t *h, play_service_attr *attr) {

}

void mgw_get_play_service(handler_t *h, play_service_attr *attr) {

}

///////////////////////////////////////////////////////////////////////////
void mgw_add_player(handler_t *h, const player_attr *attr) {

}

void mgw_release_player(handler_t *h, int channel) {

}

}