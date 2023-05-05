#include "H264Dmsp.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

H264DmspDecoder::H264DmspDecoder() {

}

void H264DmspDecoder::inputDmsp(const DmspPacket::Ptr &dmsp) {
    auto frame = FrameImp::create();
    frame->_codec_id = CodecH264;
    frame->_dts = dmsp->info.timestamp;
    if (dmsp->buffer.size() > 0) {
        frame->_buffer.append(dmsp->buffer.data(), dmsp->buffer.size());
    }
    if (dmsp->buffer.size() > 0) {
        DmspCodec::inputFrame(frame);
    }
}

void H264DmspDecoder::onGetH264(const char *data, size_t len, uint32_t dts, uint32_t pts) {

}

H264Frame::Ptr H264DmspDecoder::obtainFrame() {

}

///////////////////////////////////////////////////////////////////////////////////

H264DmspEncoder::H264DmspEncoder(const Track::Ptr &track) {
    _track = dynamic_pointer_cast<H264Track>(track);
}

bool H264DmspEncoder::inputFrame(const Frame::Ptr &frame) {
    if (frame) {
        if (!_started) {
            //该逻辑确保含有视频时，第一帧为关键帧
            if (!frame->keyFrame()) {
                //含有视频，但是不是关键帧，那么前面的帧丢弃
                return false;
            }
            //开始写文件
            _started = true;
        }
    }
    if (!_dmsp_packet) {
        _dmsp_packet = DmspPacket::create();
    }

    return _merger.inputFrame(frame, [this](uint64_t dts, uint64_t pts, const Buffer::Ptr &buffer, bool have_idr) {
                _dmsp_packet->info.frame_size = _dmsp_packet->buffer.size();
                //先暂时设置成0
                _dmsp_packet->info.stream_id = 0;
                _dmsp_packet->info.timestamp = dts;
                _dmsp_packet->info.type = have_idr ? FrameType_Key : FrameType_P;
                _dmsp_packet->info.track.bitrate = _track->getBitRate();
                _dmsp_packet->info.track.codec = EncoderId_H264;
                _dmsp_packet->info.track.tack_id = TrackVideo;
                _dmsp_packet->info.track.type = EncoderType_Video;
                _dmsp_packet->info.track.stream_info.video.width = _track->getVideoWidth();
                _dmsp_packet->info.track.stream_info.video.height = _track->getVideoHeight();
                _dmsp_packet->info.track.stream_info.video.fpsNum = _track->getVideoFps();
                _dmsp_packet->info.track.stream_info.video.fpsDen = 1;
                //输出dmsp packet
                DmspCodec::inputDmsp(_dmsp_packet);
                _dmsp_packet = nullptr;
            }, &_dmsp_packet->buffer);
}

void H264DmspEncoder::flush() {
    inputFrame(nullptr);
}

void H264DmspEncoder::makeConfigPacket() {

}

void H264DmspEncoder::makeVideoConfigPkt() {

}

}