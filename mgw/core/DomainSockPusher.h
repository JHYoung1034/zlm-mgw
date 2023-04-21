/**
 * 这个文件用于处理DomainSocket封包，然后调用DomainSock发送数据包
*/
#ifndef __MGW_CORE_DOMAIN_SOCK_PUSHER_H__
#define __MGW_CORE_DOMAIN_SOCK_PUSHER_H__

#include <stdint.h>
#include <stddef.h>

typedef uint32_t mgw_stream_id_t;

enum mgw_stream_type {
    mgw_stream_type_video = 1,
    mgw_stream_type_audio = 2,
};

enum mgw_stream_codec {
    mgw_stream_codec_h264 = 1,
    mgw_stream_codec_h265 = 2,
    mgw_stream_codec_aac = 3,
};

struct mgw_stream_track_info {
    uint8_t type; // see mgw_stream_type
    uint8_t codec; // see mgw_stream_codec
    uint16_t bitrate; // kbps
    union {
        struct video_info {
            uint16_t width;
            uint16_t height;
            uint16_t fpsNum;
            uint16_t fpsDen;
        } video;
        struct audio_info {
            uint16_t channels;
            uint16_t sameple_rate;
        } audio;
        char reserved[16];
    } stream_info;
};

enum mgw_stream_frame_type {
    mgw_stream_frame_type_def = 0,
    mgw_stream_frame_type_keyframe = 1,
    mgw_stream_frame_type_p_frame = 2,
};

struct mgw_stream_frame {
    mgw_stream_track_info track_info;
    mgw_stream_id_t stream_id;
    uint8_t type; // see mgw_stream_frame_type
    char reserved[7];
    size_t frame_size;
}; // followed by frame data of frame_size bytes

#include "DomainSock.h"
#include "Pusher/PusherBase.h"
// #include "RtmpMediaSource.h"

namespace mediakit {

class DomainSockPusher : public DomainSock/*, public PusherBase*/ {
public:
    using Ptr = std::shared_ptr<DomainSockPusher>;

    // DomainSockPusher(const DomainSock::Ptr &ptr) {}
    ~DomainSockPusher() {}

    // void publish(const std::string &url){} override ;
    // void teardown(){} override;
    // void setNetif(const std::string &netif, uint16_t mss){} override;

private:

};

}
#endif  //__MGW_CORE_DOMAIN_SOCK_PUSHER_H__