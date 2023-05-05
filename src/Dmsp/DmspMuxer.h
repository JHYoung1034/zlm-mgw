#ifndef __SRC_DMSP_MUXER_H__
#define __SRC_DMSP_MUXER_H__

#include "Dmsp/Dmsp.h"
#include "Extension/Frame.h"
#include "Common/MediaSink.h"
#include "DmspCodec.h"

namespace mediakit{

class DmspMuxer : public MediaSinkInterface {
public:
    using Ptr = std::shared_ptr<DmspMuxer>;

    /**
     * 构造函数
     */
    DmspMuxer();
    ~DmspMuxer() override = default;

    /**
     * 获取dmsp环形缓存
     * @return
     */
    DmspRing::RingType::Ptr getDmspRing() const;

    /**
     * 添加ready状态的track
     */
    bool addTrack(const Track::Ptr & track) override;

    /**
     * 写入帧数据
     * @param frame 帧
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 刷新输出所有frame缓存
     */
    void flush() override;

    /**
     * 重置所有track
     */
    void resetTracks() override ;

    /**
     * 生成config包
     */
     void makeConfigPacket();
private:
    DmspRing::RingType::Ptr _dmsp_ring;
    DmspCodec::Ptr _encoders[TrackMax];
};


} /* namespace mediakit */
#endif  //__SRC_DMSP_MUXER_H__