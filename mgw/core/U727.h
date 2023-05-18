#ifndef __MGW_CORE_U727_H__
#define __MGW_CORE_U727_H__

#include "Defines.h"
#include "StreamAuth.h"
#include "PushHelper.h"
#include "PlayHelper.h"

#include <cinttypes>
#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <unordered_set>
#include <unordered_map>

namespace mediakit {

class U727 : public std::enable_shared_from_this<U727> {
public:
    using Ptr = std::shared_ptr<U727>;

    U727();
    ~U727() {}

    void setRtmpPort(uint16_t port) { _port_map.emplace("rtmp", port); }
    uint16_t getRtmpPort() { return _port_map["rtmp"]; }
    void setSrtPort(uint16_t port) { _port_map.emplace("srt", port); }
    uint16_t getSrtPort() { return _port_map["srt"]; }
    void setHttpPort(uint16_t port) { _port_map.emplace("http", port); }
    uint16_t getHttpPort() { return _port_map["http"]; }
    void setBlackDevice(const std::string &device) { _black_list.emplace(device); }

    void stopStream(const std::string &id);

    std::string getRtspPushAddr(const std::string &stream_id);
    std::string getRtspPullAddr(const std::string &stream_id);
    bool availableRtspAddr(const std::string &url);

    /**推流器管理*/
    PushHelper::Ptr pusher(const std::string &stream_id);
    void releasePusher(const std::string &stream_id);
    void pusher_for_each(std::function<void(PushHelper::Ptr)> func);

    /**拉流播放器管理*/
    PlayHelper::Ptr player(const std::string &stream_id);
    void releasePlayer(const std::string &stream_id);
    void player_for_each(std::function<void(PlayHelper::Ptr)> func);
    /** 用于设置静态变量_static_u727 */
    void u727Ready();

    static std::weak_ptr<U727> &getU727() { return _static_u727; }

private:
    //u727启动时间
    uint32_t        _start_time;
    //推流/播放地址生成以及鉴权
    StreamAuth      _auth;
    //设备黑名单
    std::unordered_set<std::string> _black_list;
    //指定流服务的端口号
    std::unordered_map<std::string, uint16_t> _port_map;
    //推流列表
    std::unordered_map<std::string, PushHelper::Ptr> _pusher_map;
    //拉流列表
    std::unordered_map<std::string, PlayHelper::Ptr> _player_map;

    static std::weak_ptr<U727> _static_u727;
};

}
#endif  //__MGW_CORE_U727_H__