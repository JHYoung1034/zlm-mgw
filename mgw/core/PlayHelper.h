#ifndef __PLAY_HELPER_H__
#define __PLAY_HELPER_H__

#include "Player/MediaPlayer.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "Util/TimeTicker.h"
#include "Extension/Frame.h"
#include "Defines.h"

namespace MGW {

class FrameIngest;

//此类用于处理播放，可以播放本地录像文件，可以播放网络流
class PlayHelper : public mediakit::MediaPlayer, public mediakit::MediaSourceEvent, public std::enable_shared_from_this<PlayHelper> {
public:
    using Ptr = std::shared_ptr<PlayHelper>;

    //根据业务需求，在推流状态发生变化时都应该通知(正在推流，正在重连，推流失败)，简化为一个function,需要包含通道名
    /** name, status, start_time, exception */
    /** player 和 pusher 的一些业务特性相同，后续要抽象出来统一处理 */
    using onStatusChanged = std::function<void(const std::string&,
                    ChannelStatus,Time_t, const toolkit::SockException &)>;
    /** id, data, size, dts, pts, keyframe */
    using onData = std::function<void (mediakit::CodecId, const char*, uint32_t, uint64_t, uint64_t, bool)>;
    /**Video: codecid, width, height, fps, vkbps
     * Audio: codecid, channels, samplerate, samplesize, akbps
    */
    using onMeta = std::function<void (mediakit::CodecId, uint16_t, uint16_t, uint16_t, uint16_t)>;

    /// @brief 创建一个播放器
    /// @param chn 播放器所属通道
    /// @param max_retry max_retry=-1 时表示永久尝试拉流，如果是从录像文件输入，一直循环播放
    PlayHelper(const std::string &name, int chn = -1, int max_retry = -1);
    ~PlayHelper();

    /// @brief 从指定的网卡拉流，此接口仅仅在播放网络流之前设置才有效
    /// @param netif 网卡名
    /// @param mtu mtu值
    void setNetif(const std::string &netif, uint16_t mtu);

    /// @brief 启动播放器
    /// @param url 网络url或者本地录像文件路径
    /// @param on_status_changed 播放状态变化回调
    /// @param on_data 播放帧数据回调
    void start(const std::string &url, onStatusChanged on_status_changed, onData on_data, onMeta on_meta);
    void restart(const std::string &url, onStatusChanged on_status_changed, onData on_data, onMeta on_meta);

    /**
     * 获取观看总人数
     */
    int totalReaderCount();

    ChannelStatus status() const { return _info.status; }
    StreamInfo getInfo() const { return _info; }
    bool isLocalInput() const { return _is_local_input; }
private:
    //MediaSourceEvent override
    bool close(mediakit::MediaSource &sender) override;
    int totalReaderCount(mediakit::MediaSource &sender) override;
    mediakit::MediaOriginType getOriginType(mediakit::MediaSource &sender) const override;
    std::string getOriginUrl(mediakit::MediaSource &sender) const override;
    std::shared_ptr<toolkit::SockInfo> getOriginSock(mediakit::MediaSource &sender) const override;
    float getLossRate(mediakit::MediaSource &sender, mediakit::TrackType type) override;

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
    //播放状态回调
    onStatusChanged _on_status_changed;
    //收到meta信息的时候，回调到外部
    onMeta      _on_meta;
    //播放器流信息
    StreamInfo  _info;
    //重连定时器
    toolkit::Timer::Ptr _timer;
    //转换成允许的协议
    mediakit::ProtocolOption _option;
    //网络拉流，或者从录像文件输入得到的源
    mediakit::MultiMediaSourceMuxer::Ptr _muxer;
    //播放数据帧摄取
    std::shared_ptr<FrameIngest> _ingest = nullptr;
};

//////////////////////////////////////////////////////////////////////////////////
//此类用于摄取播放器帧数据
class FrameIngest : public mediakit::FrameWriterInterface, public std::enable_shared_from_this<FrameIngest> {
public:
    using Ptr = std::shared_ptr<FrameIngest>;
    FrameIngest(PlayHelper::onData on_data, PlayHelper::onMeta on_meta)
        :_on_data(on_data), _on_meta(on_meta) {}

    bool inputFrame(const mediakit::Frame::Ptr &frame)override;

private:
    PlayHelper::onData  _on_data = nullptr;
    PlayHelper::onMeta  _on_meta = nullptr;
    mediakit::CodecId   _video_eid = mediakit::CodecInvalid;
    //用于合并视频帧，把vps，sps，pps和I帧合并成IDR帧再一起发送出去
    mediakit::FrameMerger _merge = {mediakit::FrameMerger::h264_prefix};
};

}
#endif  //__PLAY_HELPER_H__