#ifndef __SRC_EXTENSION_H265_DMSP_H__
#define __SRC_EXTENSION_H265_DMSP_H__

#include "Dmsp/DmspCodec.h"
#include "Extension/Track.h"
#include "Extension/H265.h"

namespace mediakit{
/**
 * h265 Dmsp解码类
 * 将 h265 over dmsp 解复用出 h265-Frame
 */
class H265DmspDecoder : public DmspCodec {
public:
    using Ptr = std::shared_ptr<H265DmspDecoder>;

    H265DmspDecoder();
    ~H265DmspDecoder() {}

    /**
     * 输入265 Dmsp包
     * @param dmsp Dmsp包
     */
    void inputDmsp(const DmspPacket::Ptr &dmsp) override;

    CodecId getCodecId() const override{
        return CodecH265;
    }

protected:
    void onGetH265(const char *pcData, size_t iLen, uint32_t dts,uint32_t pts);
    H265Frame::Ptr obtainFrame();

protected:
    H265Frame::Ptr _h265frame;
};

/**
 * 265 Dmsp打包类
 */
class H265DmspEncoder : public H265DmspDecoder{
public:
    using Ptr = std::shared_ptr<H265DmspEncoder>;

    /**
     * 构造函数，track可以为空，此时则在inputFrame时输入sps pps
     * 如果track不为空且包含sps pps信息，
     * 那么inputFrame时可以不输入sps pps
     * @param track
     */
    H265DmspEncoder(const Track::Ptr &track);
    ~H265DmspEncoder() = default;

    /**
     * 输入265帧，可以不带sps pps
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
    bool _got_config_frame = false;
    bool _started = false;
    std::string _vps;
    std::string _sps;
    std::string _pps;
    H265Track::Ptr _track;
    DmspPacket::Ptr _dmsp_packet;
    FrameMerger _merger{FrameMerger::mp4_nal_size};
};

}//namespace mediakit
#endif  //__SRC_EXTENSION_H265_DMSP_H__