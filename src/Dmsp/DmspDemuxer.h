#ifndef __SRC_DMSP_DEMUXER_H__
#define __SRC_DMSP_DEMUXER_H__

#include <functional>
#include <unordered_map>
#include "Dmsp.h"
#include "Common/MediaSink.h"
#include "DmspCodec.h"

namespace mediakit {

class DmspDemuxer : public Demuxer {
public:
    using Ptr = std::shared_ptr<DmspDemuxer>;

    DmspDemuxer() = default;
    ~DmspDemuxer() override = default;

    // static size_t trackCount(const AMFValue &metadata);

    // bool loadMetaData(const AMFValue &metadata);

    /**
     * 开始解复用
     * @param pkt dmsp包
     */
    void inputDmsp(const DmspPacket::Ptr &pkt);

    /**
     * 获取节目总时长
     * @return 节目总时长,单位秒
     */
    float getDuration() const;

private:
    void makeVideoTrack(const DmspTack &track);
    void makeAudioTrack(const DmspTack &track);

private:
    bool _try_get_video_track = false;
    bool _try_get_audio_track = false;
    float _duration = 0.0f;
    AudioTrack::Ptr _audio_track;
    VideoTrack::Ptr _video_track;
    DmspCodec::Ptr _audio_dmsp_decoder;
    DmspCodec::Ptr _video_dmsp_decoder;
};

} /* namespace mediakit */

#endif  //__SRC_DMSP_DEMUXER_H__