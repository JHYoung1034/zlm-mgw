#ifndef __PLAY_HELPER_H__
#define __PLAY_HELPER_H__
/** 参考mk_player.cpp */
#include "Player/MediaPlayer.h"
#include "Defines.h"

namespace mediakit {

class PlayHelper : public MediaPlayer, public std::enable_shared_from_this<PlayHelper> {
public:
    using Ptr = std::shared_ptr<PlayHelper>;

    using onPlay        = std::function<void (const std::string&, ChannelStatus, Time_t, ErrorCode)>;
    using onShutdown    = std::function<void (const std::string&, ErrorCode)>;
    using onData        = std::function<void (mgw_packet *)>;

    PlayHelper(int chn);
    ~PlayHelper();

    void start(const std::string &url, onPlay on_play, onShutdown on_shutdown, int max_retry,
                onData on_data, const std::string &netif = "default", uint16_t mtu = 1500);

private:
    uint64_t    _total_bytes;
    onPlay      _on_play;
    onShutdown  _on_shutdown;
    onData      _on_data;
};

}
#endif  //__PLAY_HELPER_H__