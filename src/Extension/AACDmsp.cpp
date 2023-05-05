#include "AACDmsp.h"

using namespace std;

namespace mediakit {

void AACDmspDecoder::inputDmsp(const DmspPacket::Ptr &dmsp) {
    auto frame = FrameImp::create();
    frame->_codec_id = CodecAAC;
    frame->_dts = dmsp->info.timestamp;
    if (dmsp->buffer.size() > 0) {
        frame->_buffer.append(dmsp->buffer.data(), dmsp->buffer.size());
    }
    if (dmsp->buffer.size() > 0) {
        DmspCodec::inputFrame(frame);
    }
}

void AACDmspDecoder::onGetAAC(const char *data, size_t len, uint32_t stamp) {

}

///////////////////////////////////////////////////////////////////////////////////////////

AACDmspEncoder::AACDmspEncoder(const Track::Ptr &track) {
    _track = dynamic_pointer_cast<AACTrack>(track);
}

bool AACDmspEncoder::inputFrame(const Frame::Ptr &frame) {
    //如果此时还没有tack 信息，那么需要解析aac frame中的adts头，并保存起来
    if (_aac_cfg.empty()) {
        // _track = make_shared<AACTrack>(move(string(frame->data(), frame->prefixSize())));
        if (frame->prefixSize()> 2) {
            _aac_cfg = makeAacConfig((uint8_t*)(frame->data()), frame->prefixSize());
        }
        makeConfigPacket();
    }

    //如果没有track，每帧数据中也没有adts头，
    //那么返回失败，因为不知道音频元信息，解码器无法解码
    if (_aac_cfg.empty() && !frame->prefixSize()) {
        return false;
    }

    auto dmsp = DmspPacket::create();
    dmsp->info.frame_size = frame->size();
    dmsp->info.timestamp = frame->dts();
    dmsp->info.type = FrameType_Def;
    dmsp->info.stream_id = 0;
    dmsp->info.track.bitrate = _track->getBitRate();
    dmsp->info.track.codec = EncoderId_AAC;
    dmsp->info.track.tack_id = TrackAudio;
    dmsp->info.track.type = EncoderType_Audio;
    dmsp->info.track.stream_info.audio.channels = _track->getAudioChannel();
    dmsp->info.track.stream_info.audio.sample_bits = _track->getAudioSampleBit();
    dmsp->info.track.stream_info.audio.sample_rate = _track->getAudioSampleRate();

    dmsp->buffer.append(frame->data(), frame->size());
    DmspCodec::inputDmsp(dmsp);
}

void AACDmspEncoder::makeConfigPacket() {
    if (_track && _track->ready()) {
        _aac_cfg = _track->getAacCfg();
    }

    if (!_aac_cfg.empty()) {
        makeAudioConfigPkt();
    }
}

void AACDmspEncoder::makeAudioConfigPkt() {
    //如果是rtmp，需要生成flv的音频header
    //dmsp不需要
}

}