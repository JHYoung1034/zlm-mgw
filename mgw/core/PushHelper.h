#ifndef __MGW_PUSH_HELPER_H__
#define __MGW_PUSH_HELPER_H__

#include "Pusher/MediaPusher.h"
#include "Util/TimeTicker.h"
#include "Defines.h"

namespace mediakit {

/**
* 需要的参数：推流地址(带用户名&密码 待实现)，通道名字(O[L/R][chn]), 绑定网卡名&mtu, 推流状态回调
*/
class PushHelper : public std::enable_shared_from_this<PushHelper> {
public:
    using Ptr           = std::shared_ptr<PushHelper>;
    //根据业务需求，在推流状态发生变化时都应该通知(正在推流，正在重连，推流失败)，简化为一个function,需要包含通道名
    /** name, status, start_time, exception, user_data*/
    using onStatusChanged = std::function<void(const std::string&, ChannelStatus,
                                Time_t, const toolkit::SockException&, void*)>;

    PushHelper(const std::string &name, int channel = -1);
    ~PushHelper();
    void start(const std::string &url, onStatusChanged on_status_changed, int max_retry,
                const MediaSource::Ptr &src, const std::string &netif = "default",
                uint16_t mtu = 1500, void *userdata = nullptr);
    //对于正在推流的通道，如果有新的推流，释放原来的推流，建立一个新的推流
    void restart(const std::string &url, onStatusChanged on_status_changed,
                const MediaSource::Ptr &src, const std::string &netif = "default",
                uint16_t mtu = 1500, void *userdata = nullptr);

    ChannelStatus status() const { return _info.status; }
    StreamInfo getInfo() const { return _info; }
    void updateInfo(const StreamInfo info);

private:
    PushHelper() = delete;
    void rePublish(const std::string &url, int failed_cnt);

private:
    uint16_t _mtu;
    std::string _netif;
    int _retry_count;
    onStatusChanged _on_status_changed;
    std::weak_ptr<MediaSource> _wek_src;
    StreamInfo _info;
    MediaPusher::Ptr _pusher;
    toolkit::Timer::Ptr _timer;
};

}
#endif  //__MGW_PUSH_HELPER_H__