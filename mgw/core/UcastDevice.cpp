#include "UcastDevice.h"
#include "Common/config.h"
#include <time.h>
#include <strings.h>

using namespace std;
using namespace toolkit;

namespace mediakit {

Device::DeviceConfig::DeviceConfig(const std::string &s, const std::string &t,
                    const std::string &ven, const std::string &ver,
                    const std::string &access, uint32_t max_br, uint32_t max_4kbr,
                    uint32_t pushers, uint32_t players) : 
    sn(s), type(t), vendor(ven), version(ver), access_token(access), max_bitrate(max_br),
    max_4kbitrate(max_4kbr), max_pushers(pushers), max_players(players) {}

/// @brief //////////////////////////////////////////////////////////////////
/// @param sn 
Device::Device(const string &sn) : _start_time(::time(NULL)) {
    _cfg.sn = sn;
}

Device::~Device() {}

void Device::loadConfig(const DeviceConfig &cfg) {
    _cfg = cfg;
    _auth.updateKey(cfg.access_token);
}

PushHelper::Ptr &Device::pusher(const string &name) {
    // lock_guard<recursive_mutex> lock(_pusher_mtx);
    auto &pusher = _pusher_map[name];
    if (!pusher.operator bool()) {
        pusher = make_shared<PushHelper>(getOutputChn(name));
    }
    return pusher;
}

void Device::releasePusher(const string &name) {
    // lock_guard<recursive_mutex> lock(_pusher_mtx);
    if (_pusher_map.find(name) != _pusher_map.end())
        _pusher_map.erase(name);
}

PlayHelper::Ptr &Device::player(const std::string &name) {
    auto &player = _player_map[name];
    if (!player.operator bool()) {
        player = make_shared<PlayHelper>(getSourceChn(name));
    }
    return player;
}

void Device::releasePlayer(const std::string &name) {
    if (_player_map.find(name) != _player_map.end())
        _player_map.erase(name);
}

void Device::setAlive(bool message, bool alive) {
    lock_guard<mutex> lock(_alive_mtx);
    if (message) {
        _message_alive = alive;
    } else {
        _stream_alive = alive;
    }
}

bool Device::alive() {
    lock_guard<mutex> lock(_alive_mtx);
    return _message_alive || _stream_alive;
}

bool Device::messageAlive() {
    lock_guard<mutex> lock(_alive_mtx);
    return _message_alive;
}

bool Device::streamAlive() {
    lock_guard<mutex> lock(_alive_mtx);
    return _stream_alive;
}

//查询不同协议的播放数
uint32_t Device::players(const string &schema) {
    return _total_players.load();
}

string Device::getPushaddr(uint32_t chn, const std::string &schema) {
    if (!_cfg.push_addr.empty())
        return _cfg.push_addr;

    GET_CONFIG(uint32_t, available_time, Mgw::kUrlValidityPeriodSec);
    GET_CONFIG(string, host, Mgw::kOutHostIP);
    if (host.empty()) {
        WarnL << "host is empty!";
        return "";
    }

    if (schema == "rtmp") {
        uint16_t rtmp_port = mINI::Instance()[Rtmp::kPort];
        _cfg.push_addr = _auth.getRtmpPushAddr(host, _cfg.sn.substr(0, 8),
                            _start_time + available_time, chn, rtmp_port);
    } else if (schema == "srt") {
        uint16_t srt_port = mINI::Instance()[SrtSrv::kPort];
        _cfg.push_addr = _auth.getSrtPushAddr(host, _cfg.sn.substr(0, 8),
                            _start_time + available_time, chn, srt_port);
    }

    return _cfg.push_addr;
}

string Device::getPlayaddr(uint32_t chn, const std::string &schema) {
    string play_addr;
    GET_CONFIG(uint32_t, available_time, Mgw::kUrlValidityPeriodSec);
    GET_CONFIG(string, host, Mgw::kOutHostIP);

    if (host.empty()) {
        WarnL << "host is empty!";
        return "";
    }

    if (schema == "rtmp") {
        uint16_t rtmp_port = mINI::Instance()[Rtmp::kPort];
        play_addr = _auth.getRtmpPullAddr(host, _cfg.sn.substr(0, 8),
                            _start_time + available_time, chn, rtmp_port);
    } else if (schema == "srt") {
        uint16_t srt_port = mINI::Instance()[SrtSrv::kPort];
        play_addr = _auth.getSrtPullAddr(host, _cfg.sn.substr(0, 8),
                            _start_time + available_time, chn, srt_port);
    }
    return play_addr;
}

bool Device::availableAddr(const string &url) {
    bool result = false;
    if (url.substr(0, 7) == "rtmp://") {
        result = _auth.availableRtmpAddr(url);
    } else if (url.substr(0, 6) == "srt://") {
        result = _auth.availableSrtAddr(url);
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
recursive_mutex DeviceHelper::_mtx;
unordered_map<string, Device::Ptr> DeviceHelper::_device_map;

DeviceHelper::DeviceHelper(const string &sn) : _sn(sn) {}
DeviceHelper::~DeviceHelper() {}

Device::Ptr &DeviceHelper::device() {
    lock_guard<recursive_mutex> lock(_mtx);
    auto &device = _device_map[_sn];
    if (!device.operator bool()) {
        device = make_shared<Device>(_sn);
    }
    return device;
}

void DeviceHelper::releaseDevice() {
    lock_guard<recursive_mutex> lock(_mtx);
    if (_device_map.find(_sn) != _device_map.end())
        _device_map.erase(_sn);
}

void DeviceHelper::addPusher(const string &name, const string &url, MediaSource::Ptr src,
                    PushHelper::onPublished on_pubished, PushHelper::onShutdown on_shutdown,
                    const string &netif, uint16_t mtu) {
    GET_CONFIG(int, max_retry, Mgw::kMaxRetryPush);
    auto dev = device();
    auto pusher = dev->pusher(name);
    if (pusher->status() != ChannelStatus_Idle) {
        //如果是tunnel pusher，不能手动中断去重推，否则会影响已存在的代理推流
        if (name == TUNNEL_PUSHER) {
            return;
        }

        WarnL << "Already exist, release the old and create a new: " << name;
        pusher->restart(url, on_pubished, on_shutdown, src, netif, mtu);
    } else {
        pusher->start(url, on_pubished, on_shutdown, max_retry, src, netif, mtu);
    }
}

void DeviceHelper::releasePusher(const string &name) {
    device()->releasePusher(name);
}

void DeviceHelper::startTunnelPusher(const string &src) {
    //检查一下内部推流到mgw-server的地址是否存在
    auto dev = device();
    if (dev->_cfg.push_addr.empty()) {
        WarnL << "没有可用的内部推流地址:[" << dev->_cfg.push_addr << "]";
        return;
    }

    //检查一下指定的推流网卡是否可用
    string netif;
    uint16_t mtu = 1500;
    if (_get_netif) {
        _get_netif(netif, mtu);
        //没有指定网卡的时候，使用系统默认的网卡
        if (netif.empty()) {
            netif = "default";
        }
    }

    //设置推流事件回调函数on_published, on_shutdown
    auto on_pubished = [](const string& des, ChannelStatus sta, uint32_t ts, int err) {

    };
    auto on_shutdown = [](const string &des, int err) {
        //收到这个shutdown通知的时候，应该释放这个实例
    };

    //查找指定的源src local://localhost/device/src
    MediaInfo info(string(DEFAULT_LOCAL_INPUT) + src);
    auto media_src = MediaSource::find(info);
    if (!media_src) {
        WarnL << "没有找到本地输入流:[" << src << "]";
        return;
    }

    //内部Tunnel推流到mgw-server使用特殊的名字，不占用推流通道
    addPusher(TUNNEL_PUSHER, dev->_cfg.push_addr, media_src, on_pubished, on_shutdown, netif, mtu);
}

void DeviceHelper::stopTunnelPusher() {
    //内部Tunnel推流到mgw-server使用特殊的名字，不占用推流通道
    releasePusher(TUNNEL_PUSHER);
}

void DeviceHelper::pusher_for_each(std::function<void(PushHelper::Ptr)> func) {
    for (auto pusher : device()->_pusher_map)
        func(pusher.second);
}

//返回当前有多少播放数量
size_t DeviceHelper::players() {
    return (size_t)device()->players("");
}

//返回播放发送了多少流量
uint64_t DeviceHelper::playTotalBytes() {
    return 0;
}

//返回推流发送了多少流量
uint64_t DeviceHelper::pushTotalBytes() {
    return 0;
}

//设置允许的播放协议
void DeviceHelper::enablePlayProtocol(bool enable, bool force_stop, const std::string &proto) {
    auto dev = device();
    if (0 == ::strncasecmp(proto.data(), "rtmp", 4)) {
        //是否开启转换为rtmp/flv
        dev->_option.enable_rtmp = enable;
    } else if (0 == ::strncasecmp(proto.data(), "rtsp", 4)) {
        //是否开启转换为rtsp/webrtc
        dev->_option.enable_rtsp = enable;
    } else if (0 == ::strncasecmp(proto.data(), "hls", 3)) {
        dev->_option.enable_hls = enable;
    } else if (0 == ::strncasecmp(proto.data(), "ts", 2) ||
               0 == ::strncasecmp(proto.data(), "srt", 3)) {
        //是否开启转换为http-ts/ws-ts
        dev->_option.enable_ts = enable;
    } else if (0 == ::strncasecmp(proto.data(), "fmp4", 4)) {
        //是否开启转换为http-fmp4/ws-fmp4
        dev->_option.enable_fmp4 = enable;
    }
}

//////////////////////////////////////////////////////////
//static 函数
Device::Ptr DeviceHelper::findDevice(const std::string &name, bool like) {
    if (name.empty()) {
        return nullptr;
    }

    Device::Ptr device = nullptr;
    lock_guard<recursive_mutex> lock(_mtx);
    if (like) {
        for (auto dev : _device_map) {
            if (!dev.first.compare(0, name.size(), name)) {
                device = dev.second;
            }
        }
    } else {
        unordered_map<string, Device::Ptr>::iterator it = _device_map.find(name);
        if (it != _device_map.end()) {
            device = it->second;
        }
    }
    return device;
}

void DeviceHelper::device_for_each(std::function<void(Device::Ptr)> func) {
    lock_guard<recursive_mutex> lock(_mtx);
    for (auto dev : _device_map) {
        func(dev.second);
    }
}

/////////////////////////////////////////////////////////////////
#include <sstream>
string getOutputName(bool remote, uint32_t chn) {
    ostringstream ostr;
    ostr << (remote ? "R" : "L") << "O" << chn;
    return ostr.str();
}

string getSourceName(bool remote, uint32_t chn) {
    ostringstream ostr;
    ostr << (remote ? "R" : "L") << "S" << chn;
    return ostr.str();
}

static inline int getChn(const string &name, const string &sub) {
    int chn = -1;
    size_t pos = name.rfind(sub);
    if (pos != string::npos) {
        chn = atoi(name.substr(pos+1).c_str());
    }
    return chn;
}

int getOutputChn(const std::string &name) {
    return getChn(name, "O");
}

int getSourceChn(const string &name) {
    return getChn(name, "S");
}

void urlAddPort(std::string &url, uint16_t port) {
    std::size_t pos = url.find("://");
    if (pos == std::string::npos)
        return;

    //sub_url = host[:port]/xxx/yyy
    std::string sub_url = url.substr(pos+3);
    std::size_t ip_pos = sub_url.find("]");
    if (ip_pos != std::string::npos) {
        //IPv6
        std::size_t port_pos = sub_url.find("]:");
        if (port_pos == std::string::npos) {
            ostringstream oss;
            oss << ":" << port;
            url.insert(pos+1, oss.str());
        }
    } else {
        //IPv4
        std::size_t port_pos = sub_url.find(":");
        if (port_pos == std::string::npos) {
            ostringstream oss;
            oss << ":" << port;
            url.insert(sub_url.find("/") + pos + 3, oss.str());
        }
    }
}

}