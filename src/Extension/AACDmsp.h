#ifndef __SRC_EXTENSION_AAC_DMSP_H__
#define __SRC_EXTENSION_AAC_DMSP_H__

#include "Dmsp/DmspCodec.h"
#include "Extension/Track.h"
#include "Extension/AAC.h"

namespace mediakit{
/**
 * aac Dmsp转adts类
 */
class AACDmspDecoder : public DmspCodec{
public:
    using Ptr = std::shared_ptr<AACDmspDecoder>;

    AACDmspDecoder() {}
    ~AACDmspDecoder() {}

    /**
     * 输入Dmsp并解码
     * @param dmsp Dmsp数据包
     */
    void inputDmsp(const DmspPacket::Ptr &dmsp) override;

    CodecId getCodecId() const override{
        return CodecAAC;
    }

private:
    void onGetAAC(const char *data, size_t len, uint32_t stamp);

private:
    std::string _aac_cfg;
};


/**
 * aac adts转Dmsp类
 */
class AACDmspEncoder : public AACDmspDecoder{
public:
    using Ptr = std::shared_ptr<AACDmspEncoder>;

    /**
     * 构造函数，track可以为空，此时则在inputFrame时输入adts头
     * 如果track不为空且包含adts头相关信息，
     * 那么inputFrame时可以不输入adts头
     * @param track
     */
    AACDmspEncoder(const Track::Ptr &track);
    ~AACDmspEncoder() {}

    /**
     * 输入aac 数据，可以不带adts头
     * @param frame aac数据
     */
    bool inputFrame(const Frame::Ptr &frame) override;

    /**
     * 生成config包
     */
    void makeConfigPacket() override;

private:
    void makeAudioConfigPkt();

private:
    uint8_t _audio_flv_flags;
    AACTrack::Ptr _track;
    //adts头
    std::string _aac_cfg;
};

}//namespace mediakit

#endif  //__SRC_EXTENSION_AAC_DMSP_H__