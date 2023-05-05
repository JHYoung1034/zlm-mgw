#include "DmspMediaSourceImp.h"

using namespace std;


namespace mediakit {

DmspMediaSourceImp::DmspMediaSourceImp(const string &vhost, const string &app, const string &id, int ring_size)
    : DmspMediaSource(vhost, app, id, ring_size) {
    _demuxer = std::make_shared<DmspDemuxer>();
    _demuxer->setTrackListener(this);
}

void DmspMediaSourceImp::onWrite(DmspPacket::Ptr pkt, bool ) {
    if (!_all_track_ready || _muxer->isEnabled()) {
        _demuxer->inputDmsp(pkt);
    }

    DmspMediaSource::onWrite(std::move(pkt));
}

int DmspMediaSourceImp::totalReaderCount() {
    return readerCount() + (_muxer ? _muxer->totalReaderCount() : 0);
}

void DmspMediaSourceImp::setProtocolOption(const ProtocolOption &option) {
    //不重复生成dmsp
    _option = option;
    //不重复生成dmsp协议
    _option.enable_dmsp = false;
    _muxer = std::make_shared<MultiMediaSourceMuxer>(getVhost(), getApp(), getId(), _demuxer->getDuration(), _option);
    _muxer->setMediaListener(getListener());
    _muxer->setTrackListener(std::static_pointer_cast<DmspMediaSourceImp>(shared_from_this()));
    //让_muxer对象拦截一部分事件(比如说录像相关事件)
    MediaSource::setListener(_muxer);

    for (auto &track : _demuxer->getTracks(false)) {
        _muxer->addTrack(track);
        track->addDelegate(_muxer);
    }
}

bool DmspMediaSourceImp::addTrack(const Track::Ptr &track) {
    if (_muxer) {
        if (_muxer->addTrack(track)) {
            track->addDelegate(_muxer);
            return true;
        }
    }
    return false;
}

void DmspMediaSourceImp::addTrackCompleted() {
    if (_muxer) {
        _muxer->addTrackCompleted();
    }
}

void DmspMediaSourceImp::resetTracks() {
    if (_muxer) {
        _muxer->resetTracks();
    }
}

void DmspMediaSourceImp::onAllTrackReady() {
    _all_track_ready = true;

    if (_recreate_metadata) {
        //更新metadata
        for (auto &track : _muxer->getTracks()) {
            Metadata::addTrack(_metadata, track);
        }
        // DmspMediaSource::updateMetaData(_metadata);
    }
}

void DmspMediaSourceImp::setListener(const weak_ptr<MediaSourceEvent> &listener) {
    if (_muxer) {
        //_muxer对象不能处理的事件再给listener处理
        _muxer->setMediaListener(listener);
    } else {
        //未创建_muxer对象，事件全部给listener处理
        MediaSource::setListener(listener);
    }
}

}