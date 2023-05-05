#ifndef __SRC_DMSP_DMSP_H__
#define __SRC_DMSP_DMSP_H__

#include <cinttypes>
#include <cstddef>
#include <memory>
#include "core/Defines.h"
#include "Network/Buffer.h"
#include "Util/util.h"

namespace mediakit {

typedef uint32_t StreamId;
typedef uint8_t TrackId;

struct DmspTack {
    EncoderType type;
    EncoderId codec;
    uint16_t bitrate; // kbps
    TrackId tack_id;
    char reserved1[3];
    union {
        struct VidoeInfo {
            uint16_t width;
            uint16_t height;
            uint16_t fpsNum;
            uint16_t fpsDen;
            char reserved[4];
        } video;
        struct AudioInfo {
            uint16_t channels;
            uint16_t sample_rate;
            uint8_t  sample_bits;
            char reserved[7];
        } audio;
    } stream_info;
};

enum FrameType : uint8_t {
    FrameType_Def = 0,
    FrameType_Key = 1,
    FrameType_P = 2,
};

struct StreamFrame {
    DmspTack track;
    StreamId stream_id;
    uint32_t frame_size;
    uint64_t timestamp;
    FrameType type; // see FrameType
    char reserved[7];
}; // followed by frame data of frame_size bytes

class DmspPacket : public toolkit::Buffer {
public:
    using Ptr = std::shared_ptr<DmspPacket>;
    StreamFrame info;
    toolkit::BufferLikeString buffer;

public:
    static Ptr create();

    char *data() const override{
        return (char*)buffer.data();
    }
    size_t size() const override {
        return buffer.size();
    }

    void clear();

    bool isVideoKeyFrame() const;

    int getMediaType() const;

    int getAudioSampleRate() const;
    int getAudioSampleBit() const;
    int getAudioChannel() const;

private:
    friend class toolkit::ResourcePool_l<DmspPacket>;
    DmspPacket(){
        clear();
    }

    DmspPacket &operator=(const DmspPacket &that);

private:
    //对象个数统计
    toolkit::ObjectStatistic<DmspPacket> _statistic;
};

}
#endif  //__SRC_DMSP_DMSP_H__