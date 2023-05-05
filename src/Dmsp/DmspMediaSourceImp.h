#ifndef __SRC_DMSP_MEDIA_SOURCE_IMP_H__
#define __SRC_DMSP_MEDIA_SOURCE_IMP_H__

#include <mutex>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include "Dmsp.h"
#include "DmspDemuxer.h"
#include "DmspMediaSource.h"
#include "Common/MultiMediaSourceMuxer.h"

namespace mediakit {

class DmspMediaSourceImp final : public DmspMediaSource,
        private TrackListener, public MultiMediaSourceMuxer::Listener {
public:
    using Ptr = std::shared_ptr<DmspMediaSourceImp>;

    /**
     * 构造函数
     * @param vhost 虚拟主机
     * @param app 应用名
     * @param id 流id
     * @param ringSize 环形缓存大小
     */
    DmspMediaSourceImp(const std::string &vhost, const std::string &app, const std::string &id, int ring_size = DMSP_GOP_SIZE);

    ~DmspMediaSourceImp() override = default;

    // /**
    //  * 设置metadata
    //  */
    // void setMetaData(const AMFValue &metadata) override;

    /**
     * 输入dmsp并解析
     */
    void onWrite(DmspPacket::Ptr pkt, bool = true) override;

    /**
     * 获取观看总人数，包括(hls/rtsp/rtmp/dmsp)
     */
    int totalReaderCount() override;

    /**
     * 设置协议转换
     */
    void setProtocolOption(const ProtocolOption &option);

    const ProtocolOption &getProtocolOption() const {
        return _option;
    }

    /**
     * _demuxer触发的添加Track事件
     */
    bool addTrack(const Track::Ptr &track) override;

    /**
     * _demuxer触发的Track添加完毕事件
     */
    void addTrackCompleted() override;

    void resetTracks() override;

    /**
     * _muxer触发的所有Track就绪的事件
     */
    void onAllTrackReady() override;

    /**
     * 设置事件监听器
     * @param listener 监听器
     */
    void setListener(const std::weak_ptr<MediaSourceEvent> &listener) override;

private:
    bool _all_track_ready = false;
    bool _recreate_metadata = false;
    ProtocolOption _option;
    AMFValue _metadata;
    DmspDemuxer::Ptr _demuxer;
    MultiMediaSourceMuxer::Ptr _muxer;

};

}
#endif  //__SRC_DMSP_MEDIA_SOURCE_IMP_H__