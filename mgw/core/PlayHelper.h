#ifndef __PLAY_HELPER_H__
#define __PLAY_HELPER_H__

#include "Player/MediaPlayer.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "Util/TimeTicker.h"
#include "Extension/Frame.h"
#include "Defines.h"

namespace mediakit {

class FrameIngest;

//此类用于处理播放，可以播放本地录像文件，可以播放网络流
class PlayHelper : public MediaPlayer, public MediaSourceEvent, public std::enable_shared_from_this<PlayHelper> {
public:
    using Ptr = std::shared_ptr<PlayHelper>;

    using onPlay        = std::function<void (const std::string&/*description*/, ChannelStatus, Time_t, ErrorCode)>;
    using onShutdown    = std::function<void (const std::string&/*description*/, ErrorCode)>;
    using onData        = std::function<void (mgw_packet *)>;

    /// @brief 创建一个播放器
    /// @param chn 播放器所属通道
    /// @param max_retry max_retry=-1 时表示永久尝试拉流，如果是从录像文件输入，一直循环播放
    PlayHelper(int chn, int max_retry);
    PlayHelper(const std::string &stream_id, int max_retry);
    ~PlayHelper();

    /// @brief 从指定的网卡拉流，此接口仅仅在播放网络流之前设置才有效
    /// @param netif 网卡名
    /// @param mtu mtu值
    void setNetif(const std::string &netif, uint16_t mtu);

    /// @brief 启动播放器
    /// @param url 网络url或者本地录像文件路径
    /// @param on_play 播放结果回调
    /// @param on_shutdown 播放停止回调
    /// @param on_data 播放帧数据回调
    void start(const std::string &url, onPlay on_play, onShutdown on_shutdown, onData on_data);
    void restart(const std::string &url, onPlay on_play, onShutdown on_shutdown, onData on_data);

    /**
     * 获取观看总人数
     */
    int totalReaderCount();

    ChannelStatus status() const { return _info.status; }
    StreamInfo getInfo() const { return _info; }
    bool isLocalInput() const { return _is_local_input; }
private:
    //MediaSourceEvent override
    bool close(MediaSource &sender) override;
    int totalReaderCount(MediaSource &sender) override;
    MediaOriginType getOriginType(MediaSource &sender) const override;
    std::string getOriginUrl(MediaSource &sender) const override;
    std::shared_ptr<toolkit::SockInfo> getOriginSock(MediaSource &sender) const override;
    float getLossRate(MediaSource &sender, TrackType type) override;

    /////////////////////////////////////////////////////////////////////////////////////////////
    void startFromFile(const std::string &file);
    void startFromNetwork(const std::string &url);
    //处理播放成功，生成媒体源，生成帧摄取，处理连接数据回调
    void onPlaySuccess();
    //内部拉流失败尝试重新拉流
    void rePlay(const std::string &url, int failed_cnt);
    void setDirectProxy();

private:
    //是否从本地录像文件输入
    bool _is_local_input = false;
    //网卡信息
    uint16_t _mtu;
    std::string _netif;
    //拉流后生成的源，重命名为指定的stream_id，否则根据url的stream_id命名
    std::string _stream_id;
    //失败最多重连次数
    int _max_retry;
    //开始播放，结果回调
    onPlay      _on_play;
    //收到播放终止回调
    onShutdown  _on_shutdown;
    //收到数据帧的时候，回调到外部
    onData      _on_data;
    //播放器流信息
    StreamInfo  _info;
    //重连定时器
    toolkit::Timer::Ptr _timer;
    //转换成允许的协议
    ProtocolOption _option;
    //网络拉流，或者从录像文件输入得到的源
    MultiMediaSourceMuxer::Ptr _muxer;
    //播放数据帧摄取
    std::shared_ptr<FrameIngest> _ingest = nullptr;
};

//////////////////////////////////////////////////////////////////////////////////
//此类用于摄取播放器帧数据后回调音视频frame给外部
class FrameIngest : public FrameWriterInterface, public std::enable_shared_from_this<FrameIngest> {
public:
    using Ptr = std::shared_ptr<FrameIngest>;
    FrameIngest(PlayHelper::onData on_data):_on_data(on_data){}

    bool inputFrame(const Frame::Ptr &frame)override;

private:
    PlayHelper::onData  _on_data = nullptr;
    CodecId             _video_eid = CodecInvalid;
    //用于合并视频帧，把vps，sps，pps和I帧合并成IDR帧再一起发送出去
    FrameMerger _merge = {FrameMerger::mp4_nal_size};
};

}
#endif  //__PLAY_HELPER_H__