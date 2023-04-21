#ifndef __MGW_PUSH_HELPER_H__
#define __MGW_PUSH_HELPER_H__

#include "Pusher/MediaPusher.h"
#include "Util/TimeTicker.h"
#include "Defines.h"

namespace mediakit {

/**
* 需要的参数：推流地址(带用户名&密码)，通道名字(O[L/R][chn]), 绑定网卡名&mtu, 推流成功&失败回调，内部关闭回调, 查找源回调
*/
class PushHelper : public std::enable_shared_from_this<PushHelper> {
public:

    using Ptr           = std::shared_ptr<PushHelper>;
    using onPublished   = std::function<void (const std::string&, ChannelStatus, Time_t, enum ErrorCode)>;
    using onShutdown    = std::function<void (const std::string&, int)>;

    PushHelper(int chn);
    ~PushHelper();
    void start(const std::string &url, onPublished on_pubished, onShutdown on_shutdown, int max_retry,
                const MediaSource::Ptr &src, const std::string &netif = "default", uint16_t mtu = 1500);
    //对于正在推流的通道，如果有新的推流，释放原来的推流，建立一个新的推流
    void restart(const std::string &url, onPublished on_pubished, onShutdown on_shutdown,
                const MediaSource::Ptr &src, const std::string &netif = "default", uint16_t mtu = 1500);

    ChannelStatus status() const { return _info.status; }
    StreamInfo getInfo() const { return _info; }

private:
    PushHelper() = delete;
    void rePublish(const std::string &url, int failed_cnt);

private:
    uint16_t _mtu;
    std::string _netif;
    int _retry_count;
    onPublished _on_pubished;
    onShutdown _on_shutdown;
    std::weak_ptr<MediaSource> _wek_src;
    StreamInfo _info;
    MediaPusher::Ptr _pusher;
    toolkit::Timer::Ptr _timer;
};

}
#endif  //__MGW_PUSH_HELPER_H__