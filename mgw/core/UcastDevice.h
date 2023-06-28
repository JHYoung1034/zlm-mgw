/** 
 * 实现对ucast设备的描述，以及设备相关资源管理.
 * 可以分为两种设备实例，一种是Ucast设备端使用的，一种是mgw-server服务端使用的SessionDevice
 * 两种实例有一些相似处，也有一些不同的地方，比如处理的任务可能有一些不一样。
*/

#ifndef __MGW_UCAST_DEVICE_H__
#define __MGW_UCAST_DEVICE_H__

#include <memory>
#include <string>
#include <mutex>
#include <set>
#include <atomic>
#include <unordered_map>
#include "Util/logger.h"
#include "StreamAuth.h"
#include "PushHelper.h"
#include "PlayHelper.h"
#include "TrafficStatistics.h"
#include "Defines.h"

namespace MGW {

class Device : public toolkit::DeviceBase {
public:
    struct DeviceConfig;

    using Ptr = std::shared_ptr<Device>;
    using onNoReader = std::function<void(const std::string&/*id*/)>;
    using onPlayersChanged = std::function<void(bool/*local*/, int/*players*/)>;
    using onNotFoundStream = std::function<void(const std::string &)>;
    using onConfigChanged = std::function<void(const DeviceConfig &)>;
    using onStatusChanged = std::function<void(ChannelType, ChannelId, ChannelStatus, ErrorCode, Time_t, void *)>;
    using onAuthen = std::function<bool(const std::string &)>;

    friend class DeviceHelper;

    struct DeviceConfig {
        friend class Device;
        std::string     sn;
        std::string     type;
        std::string     vendor;
        std::string     version;
        std::string     access_token;

        std::string     push_addr;
        std::string     play_addr;

        uint32_t        max_bitrate = 0;
        uint32_t        max_4kbitrate = 0;
        uint32_t        max_pushers = 0;
        uint32_t        max_players = 0;
        DeviceConfig() {}
        DeviceConfig(const std::string &s, const std::string &t,
                    const std::string &ven, const std::string &ver,
                    const std::string &access, uint32_t max_br, uint32_t max_4kbr,
                    uint32_t pushers, uint32_t players);
        void setPushAddr(const std::string &addr) {
            push_addr = addr;
        }
        void setPlayAddr(const std::string &addr) {
            play_addr = addr;
        }
    };

    Device(const std::string &sn);
    ~Device();
    //创建一个设备后，需要调用此函数加载设备信息。
    void loadConfig(const DeviceConfig &cfg);
    DeviceConfig getConfig() { return _cfg; }
    void updateToken(const std::string &token) { _cfg.access_token = token; }

    //设置设备消息和流是否活跃
    bool alive();
    bool streamAlive();
    bool messageAlive();
    void setAlive(bool message, bool alive);
    //查询不同协议的播放数
    uint32_t players(bool local, const std::string &schema);
    //生成推流地址和播放地址，url合法性校验
    std::string getPushaddr(uint32_t chn, const std::string &schema, bool remote = false);
    std::string getPlayaddr(uint32_t chn, const std::string &schema, bool remote = false);
    bool availableAddr(const std::string &url);
    ////////////DeviceBase///////////////
    std::string sn(void) override { return _cfg.sn; }

    ///////////////// 处理事件 ////////////////
    void doOnNoReader(const std::string &id) {
        if (_on_noreader) {
            _on_noreader(id);
        }
    }
    void doOnPlayersChange(bool local, int players) {
        local ? (_local_players = players) : (_remote_players = players);
        if (_on_players_changed) {
            _on_players_changed(local, players);
        }
    }
    void doOnNotFoundStream(const std::string &full_url) {
        if (_on_notfound_stream) {
            _on_notfound_stream(full_url);
        }
    }
    void doOnConfigChanged(const DeviceConfig &config) {
        if (_on_config_changed) {
            _on_config_changed(config);
        }
    }
    void doOnStatusChanged(ChannelType type, ChannelId chn,
                ChannelStatus sta, ErrorCode code, Time_t ts) {
        if (_on_status_changed) {
            //当服务器的推流状态变化时，发送消息到设备端，设备端ws_client会调用这个回调
            //需要查找出对应的pusher实例，提取user_data，再通知到设备业务处理，一般是remote output
            void *userdata = nullptr;
            if (type == ChannelType_ProxyOutput) {
                auto pusher = _pusher_map[getOutputName(true, chn)];
                if (pusher) {
                    userdata = pusher->getInfo().userdata;
                }
            }
            _on_status_changed(type, chn, sta, code, ts, userdata);
        }
    }
    bool doOnAuthen(const std::string &full_url) {
        if (!_on_authen) {
            return false;
        }
        return _on_authen(full_url);
    }
    ////////////////////////////////////////////

    mediakit::ProtocolOption getEnableOption() const { return _option; }

    //管理推流
    PushHelper::Ptr &pusher(const std::string &name);
    void releasePusher(const std::string &name);
    void pusher_for_each(std::function<void(PushHelper::Ptr)> func);
    //管理拉流播放
    PlayHelper::Ptr &player(const std::string &name);
    void releasePlayer(const std::string &name);
    void player_for_each(std::function<void(PlayHelper::Ptr)> func);

private:
    //设备启动时间
    uint32_t        _start_time;
    //设备消息通道和流通道是否活跃标记，多线程操作，需要加锁
    //一般在流建立和注销，消息会话连接和断连时会操作，低频操作，加锁不会影响性能
    std::mutex      _alive_mtx;
    bool            _message_alive = false;
    bool            _stream_alive = false;

    //设置一些事件回调，用于发送通知给设备端
    onNoReader _on_noreader = nullptr;
    onPlayersChanged _on_players_changed = nullptr;
    onNotFoundStream _on_notfound_stream = nullptr;

    ////////////////////设备端回调/////////////////////////
    onConfigChanged _on_config_changed = nullptr;
    onStatusChanged _on_status_changed = nullptr;

    /////////////////处理推拉流鉴权////////////////////////
    onAuthen _on_authen = nullptr;

    //收到该设备流的消费者变化时，记录在此，在状态同步时下发给设备
    //由于收到消费者变化的消息是由其他线程调用，因此该变量使用原子操作。
    std::atomic<uint32_t> _local_players = {0};
    std::atomic<uint32_t> _remote_players = {0};

    //流量统计，应该跟随设备实例生命周期，累计统计，不能出现反转
    TrafficsStatistics::Ptr _tra_sta = nullptr;

    //设备配置信息
    DeviceConfig    _cfg;
    //鉴权实例，包括生成推流和播放地址
    StreamAuth      _auth;
    //允许的播放协议,如：rtmp, hls, rtsp, http-flv等等协议，默认根据配置文件设置。
    //当需要录制这一个设备流的时候，可以更改这个option
    mediakit::ProtocolOption _option;
    //推流实例管理，u727会话线程会访问，可能需要加锁，并不会频繁访问
    std::unordered_map<std::string, PushHelper::Ptr> _pusher_map;
    //拉流实例管理
    std::unordered_map<std::string, PlayHelper::Ptr> _player_map;
};

//用于分离Device和MessageSession,管理设备实例,处理Device一些简单的属性设置和获取
//实际复杂的工作还是让Device去做
class DeviceHelper : public std::enable_shared_from_this<DeviceHelper> {
public:
    using Ptr = std::shared_ptr<DeviceHelper>;
    using getNetif = std::function<void(std::string &, uint16_t &)>;   //回调获取指定网卡和mtu

    DeviceHelper(const std::string &sn, const toolkit::EventPoller::Ptr &poller);
    ~DeviceHelper();

    std::string sn() const { return _sn; }

    Device::Ptr &device();
    void releaseDevice();

    //推流
    void addPusher(const std::string &name, bool remote, const std::string &url,
                    mediakit::MediaSource::Ptr src, PushHelper::onStatusChanged on_status_changed,
                    const std::string &netif = "default", uint16_t mtu = 1500, void *userdata = nullptr);
    void releasePusher(const std::string &name);
    bool hasPusher(const std::string &name);
    const PushHelper::Ptr &getPusher(const std::string &name);
    //拉流
    void addPlayer(const std::string &name, bool remote, const std::string &url,
                PlayHelper::onStatusChanged on_status, PlayHelper::onData on_data,
                PlayHelper::onMeta on_meta, const std::string &netif = "default",
                uint16_t mtu = 1500, void *userdata = NULL);
    void releasePlayer(const std::string &name);
    bool hasPlayer(const std::string &name);
    //使用mgw-server下发的地址开启tunnel推流
    void startTunnelPusher(const mediakit::MediaSource::Ptr &media_src);
    void stopTunnelPusher();
    //遍历所有的推流
    void pusher_for_each(std::function<void(PushHelper::Ptr)> func);
    //返回当前有多少播放数量
    size_t players(bool local);
    //返回播放发送了多少流量
    uint64_t playTotalBytes();
    //返回推流发送了多少流量
    uint64_t pushTotalBytes();
    //设置允许的播放协议
    void enablePlayProtocol(bool enable, bool force_stop, const std::string &proto);
    //获取所在线程
    const toolkit::EventPoller::Ptr &getPoller() const {
        return _poller;
    }

    //////////////////////////////////////////////////////////////////
    //设置没有消费者通知回调
    void setOnNoReader(Device::onNoReader no_reader) {
        device()->_on_noreader = no_reader;
    }
    //设置播放数改变通知回调
    void setOnPlayersChange(Device::onPlayersChanged change) {
        device()->_on_players_changed = change;
    }
    //设置找不到设备流的时候通知设备推流回调
    void setOnNotFoundStream(Device::onNotFoundStream not_found) {
        device()->_on_notfound_stream = not_found;
    }
    //设备配置发生了变化
    void setOnConfigChanged(Device::onConfigChanged config_changed) {
        device()->_on_config_changed = config_changed;
    }
    //某个pusher或者player状态发生了变化
    void setOnStatusChanged(Device::onStatusChanged status_changed) {
        device()->_on_status_changed = status_changed;
    }
    //当收到推流或者拉流事件时，调用鉴权处理
    void setOnAuthen(Device::onAuthen authen) {
        device()->_on_authen = authen;
    }
    /////////////////////////////////////////////////////////////////////
    void setOnNetif(getNetif get_netif) {
        if (_get_netif) {
            _get_netif = get_netif;
        }
    }
    //静态成员，用于外部查找设备实例
    static Device::Ptr findDevice(const std::string &name, bool like = true);
    static void device_for_each(std::function<void(Device::Ptr)> func);

private:
    DeviceHelper() = delete;

private:
    std::string _sn;
    //对设备的流量统计
    TrafficsStatistics _flow_sta;
    //回调获取指定网卡和mtu
    getNetif _get_netif = nullptr;
    //线程实例
    toolkit::EventPoller::Ptr _poller;
    //管理设备实例
    static std::recursive_mutex _mtx;
    static std::unordered_map<std::string, Device::Ptr> _device_map;
};

}

#endif  //__MGW_UCAST_DEVICE_H__