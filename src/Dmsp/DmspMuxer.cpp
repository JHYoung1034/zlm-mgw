#include "DmspMuxer.h"
#include "Extension/Factory.h"

using namespace std;

namespace mediakit {

DmspMuxer::DmspMuxer() {
    _dmsp_ring = make_shared<DmspRing::RingType>();
}

bool DmspMuxer::addTrack(const Track::Ptr &track) {
    auto &encoder = _encoders[track->getTrackType()];
    encoder = Factory::getDmspCodecByTrack(track->clone(), true);
    if (!encoder) {
        return false;
    }

    encoder->setDmspRing(_dmsp_ring);
    return true;
}

bool DmspMuxer::inputFrame(const Frame::Ptr &frame) {
    auto &encoder = _encoders[frame->getTrackType()];
    return encoder ? encoder->inputFrame(frame) : false;
}

DmspRing::RingType::Ptr DmspMuxer::getDmspRing() const {
    return _dmsp_ring;
}

void DmspMuxer::flush() {
    //刷新所有编码track的缓存
    for (auto &encoder : _encoders) {
        if (encoder) {
            encoder->flush();
        }
    }
}

void DmspMuxer::resetTracks() {
    //把encoder_map清空
    for (auto &encoder : _encoders) {
        if (encoder) {
            encoder = nullptr;
        }
    }
}

void DmspMuxer::makeConfigPacket() {
    for (auto &encoder : _encoders) {
        if (encoder) {
            encoder->makeConfigPacket();
        }
    }
}

}