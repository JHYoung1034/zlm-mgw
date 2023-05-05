#include "Dmsp.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

DmspPacket::Ptr DmspPacket::create() {
    return Ptr(new DmspPacket);
}

void DmspPacket::clear() {
    buffer.clear();
}

bool DmspPacket::isVideoKeyFrame() const {
    return info.type == FrameType_Key;
}

int DmspPacket::getMediaType() const {
    return (int)info.track.codec;
}

int DmspPacket::getAudioSampleRate() const {
    return (int)info.track.stream_info.audio.sample_rate;
}

int DmspPacket::getAudioSampleBit() const {
    return (int)info.track.stream_info.audio.sample_bits;
}

int DmspPacket::getAudioChannel() const {
    return (int)info.track.stream_info.audio.channels;
}

DmspPacket &DmspPacket::operator=(const DmspPacket &that) {
    info = that.info;
    return *this;
}

}

namespace toolkit {
    StatisticImp(mediakit::DmspPacket);
}