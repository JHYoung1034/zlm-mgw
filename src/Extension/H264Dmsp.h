#ifndef __SRC_EXTENSION_H264_DMSP_H__
#define __SRC_EXTENSION_H264_DMSP_H__

#include "Dmsp/DmspCodec.h"
#include "Extension/Track.h"
#include "Extension/H264.h"

namespace mediakit{
/**
 * h264 Dmsp解码类
 * 将 h264 over Dmsp 解复用出 h264-Frame
 */
class H264DmspDecoder : public DmspCodec {
public:
    using Ptr = std::shared_ptr<H264DmspDecoder>;

    H264DmspDecoder();
    ~H264DmspDecoder() {}

    /**
     * 输入264 Dmsp包
     * @param dmsp Dmsp包
     */
    void inputDmsp(const DmspPacket::Ptr &dmsp) override;

    CodecId getCodecId() const override{
        return CodecH264;
    }

protected:
    void onGetH264(const char *data, size_t len, uint32_t dts, uint32_t pts);
    H264Frame::Ptr obtainFrame();

protected:
    H264Frame::Ptr _h264frame;
    std::string _sps;
    std::string _pps;
};

/**
 * 264 Dmsp打包类
 */
class H264DmspEncoder : public H264DmspDecoder{
public:
    using Ptr = std::shared_ptr<H264DmspEncoder>;

    /**
     * 构造函数，track可以为空，此时则在inputFrame时输入sps pps
     * 如果track不为空且包含sps pps信息，
     * 那么inputFrame时可以不输入sps pps
     * @param track
     */
    H264DmspEncoder(const Track::Ptr &track);
    ~H264DmspEncoder() = default;

    /**
     * 输入264帧，可以不带sps pps
     * @param frame 帧数据
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 刷新输出所有frame缓存
     */
    void flush() override;

    /**
     * 生成config包
     */
    void makeConfigPacket() override;

private:
    void makeVideoConfigPkt();

private:
    bool _started = false;
    bool _got_config_frame = false;
    H264Track::Ptr _track;
    DmspPacket::Ptr _dmsp_packet;
    FrameMerger _merger{FrameMerger::mp4_nal_size};
};

}//namespace mediakit


#endif  //__SRC_EXTENSION_H264_DMSP_H__