#include "PlayHelper.h"
#include "Common/macros.h"
#include "Rtmp/RtmpMediaSource.h"
#include "Rtsp/RtspMediaSource.h"
#include "Rtmp/RtmpPlayer.h"
#include "Rtsp/RtspPlayer.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

PlayHelper::PlayHelper(int chn, int max_retry) {
    _info.channel = chn;
    _max_retry = max_retry;
}

PlayHelper::PlayHelper(const string &stream_id, int max_retry) {
    _info.channel = 0;
    _max_retry = max_retry;
    _stream_id = stream_id;
}

PlayHelper::~PlayHelper() {
    _timer.reset();
    if (_on_play) {
        _on_play(_info.url, ChannelStatus_Idle, ::time(NULL), Success);
        _on_play = nullptr;
    }
}

void PlayHelper::setNetif(const string &netif, uint16_t mtu) {
    MediaPlayer::setNetif(netif, mtu);
}

void PlayHelper::start(const string &url, onPlay on_play, onShutdown on_shutdown, onData on_data) {
    _info.url = url;
    _on_play = on_play;
    _on_shutdown = on_shutdown;
    _on_data = on_data;
    _info.startTime = ::time(NULL);

    if (_stream_id.empty()) {
        _stream_id = FindField(url.data(), url.substr(url.rfind('/')+1).data(), NULL);
    }

    if (on_data && !_ingest) {
        _ingest = make_shared<FrameIngest>(on_data);
    } else {
        _ingest = nullptr;
    }

    if (url.find("://") != string::npos) {
        startFromNetwork(url);
    } else if (access(url.data(), F_OK) == 0) {
        startFromFile(url);
    } else {
        WarnL << "Invalid url: " << url;
    }
}
void PlayHelper::restart(const string &url, onPlay on_play, onShutdown on_shutdown, onData on_data) {
    if (_info.status != ChannelStatus_Idle) {
        teardown();
    }
    start(url, on_play, on_shutdown, on_data);
}

void PlayHelper::startFromFile(const string &file) {
    _is_local_input = true;
    onPlaySuccess();
}

void PlayHelper::startFromNetwork(const string &url) {
    weak_ptr<PlayHelper> weak_self = shared_from_this();
    //初始化为-1，如果第一次拉流失败则放弃尝试重连
    shared_ptr<int> failed_cnt(new int(-1));

    auto do_retry = [weak_self, url, failed_cnt](const SockException &err) {
        auto strong_self = weak_self.lock();
        if (!strong_self) { return; }

        if ((*failed_cnt) >= 0 && (*failed_cnt < strong_self->_max_retry || strong_self->_max_retry < 0)) {
            /**
             * (*failed_cnt) >= 0 说明有过成功拉流
             * _max_retry 是小于0，或者failed_cnt小于_max_retry才可以重新拉流
             */
            strong_self->rePlay(url, (*failed_cnt)++);
        } else {
            //发生了错误，并且没办法重新拉流了，回调通知
            strong_self->_info.stopTime = ::time(NULL);
            if (strong_self->_on_shutdown) {
                strong_self->_on_shutdown("player shutdown", (ErrorCode)err.getErrCode());
            }
        }
    };

    setOnPlayResult([weak_self, url, failed_cnt, do_retry](const SockException &err) {
        auto strong_self = weak_self.lock();
        if (!strong_self) { return; }

        if (err.operator bool()) {
            //发生了异常
            strong_self->_info.stopTime = ::time(NULL);
            strong_self->_info.status = ChannelStatus_Idle;
            do_retry(err);
        } else {
            strong_self->_timer.reset();
            *failed_cnt = 0;
            strong_self->_info.startTime = ::time(NULL);
            strong_self->_info.status = ChannelStatus_Playing;
            strong_self->onPlaySuccess();
            InfoL << "Play [" << strong_self->_info.url << "] success";
        }

        //播放结果仅回调一次
        if (strong_self->_on_play) {
            DebugL << "Play Result: [" << err.getErrCode() << "]";
            strong_self->_on_play("success", strong_self->_info.status,
                        strong_self->_info.startTime, (ErrorCode)err.getErrCode());
            strong_self->_on_play = nullptr;
        }
    });

    setOnShutdown(do_retry);
    //调用play的时候，会根据url，判断是何种协议，在调用createPlayer创建指定协议的player
    MediaPlayer::play(url);
    setDirectProxy();
}

void PlayHelper::setDirectProxy() {
    MediaSource::Ptr mediaSource;
    if (dynamic_pointer_cast<RtspPlayer>(_delegate)) {
        //rtsp拉流
        GET_CONFIG(bool, directProxy, Rtsp::kDirectProxy);
        if (directProxy) {
            mediaSource = std::make_shared<RtspMediaSource>(DEFAULT_VHOST, "live", _stream_id);
        }
    } else if (dynamic_pointer_cast<RtmpPlayer>(_delegate)) {
        //rtmp拉流,rtmp强制直接代理
        mediaSource = std::make_shared<RtmpMediaSource>(DEFAULT_VHOST, "live", _stream_id);
    }
    if (mediaSource) {
        setMediaSource(mediaSource);
    }
}

void PlayHelper::rePlay(const std::string &url, int failed_cnt) {
    auto iDelay = MAX(2 * 1000, MIN(failed_cnt * 3000, 60 * 1000));
    weak_ptr<PlayHelper> weakSelf = shared_from_this();
    _timer = std::make_shared<Timer>(iDelay / 1000.0f, [weakSelf, url, failed_cnt]() {
        //播放失败次数越多，则延时越长
        auto strongPlayer = weakSelf.lock();
        if (!strongPlayer) {
            return false;
        }
        WarnL << "重试播放[" << failed_cnt << "]:" << url;
        strongPlayer->MediaPlayer::play(url);
        strongPlayer->setDirectProxy();
        return false;
    }, getPoller());
}

////////////////////////////////////////////////////////////////////////////////////////////////
bool PlayHelper::close(MediaSource &sender) {
    //通知其停止推流
    weak_ptr<PlayHelper> weakSelf = dynamic_pointer_cast<PlayHelper>(shared_from_this());
    getPoller()->async_first([weakSelf]() {
        auto strongSelf = weakSelf.lock();
        if (!strongSelf) {
            return;
        }
        strongSelf->_muxer.reset();
        strongSelf->setMediaSource(nullptr);
        strongSelf->teardown();
    });
    if (_on_shutdown) {
        _on_shutdown("closed by user", Success);
    }
    WarnL << "close media: " << sender.getUrl();
    return true;
}

int PlayHelper::totalReaderCount() {
    return (_muxer ? _muxer->totalReaderCount() : 0) + (_media_src ? _media_src->readerCount() : 0);
}

int PlayHelper::totalReaderCount(MediaSource &sender) {
    return totalReaderCount();
}

MediaOriginType PlayHelper::getOriginType(MediaSource &sender) const {
    return MediaOriginType::pull;
}

string PlayHelper::getOriginUrl(MediaSource &sender) const {
    return _info.url;
}

std::shared_ptr<SockInfo> PlayHelper::getOriginSock(MediaSource &sender) const {
    return getSockInfo();
}

float PlayHelper::getLossRate(MediaSource &sender, TrackType type) {
    return getPacketLossRate(type);
}

void PlayHelper::onPlaySuccess() {
    GET_CONFIG(bool, reset_when_replay, General::kResetWhenRePlay);
    if (dynamic_pointer_cast<RtspMediaSource>(_media_src)) {
        //rtsp拉流代理
        if (reset_when_replay || !_muxer) {
            _option.enable_rtsp = false;
            _muxer = std::make_shared<MultiMediaSourceMuxer>(DEFAULT_VHOST, "live", _stream_id, getDuration(), _option);
        }
    } else if (dynamic_pointer_cast<RtmpMediaSource>(_media_src)) {
        //rtmp拉流代理
        if (reset_when_replay || !_muxer) {
            _option.enable_rtmp = false;
            _muxer = std::make_shared<MultiMediaSourceMuxer>(DEFAULT_VHOST, "live", _stream_id, getDuration(), _option);
        }
    } else {
        //其他拉流代理
        if (reset_when_replay || !_muxer) {
            _muxer = std::make_shared<MultiMediaSourceMuxer>(DEFAULT_VHOST, "live", _stream_id, getDuration(), _option);
        }
        //从本地MP4文件输入
        if (_is_local_input) {
            _media_src = MediaSource::createFromMP4("", DEFAULT_VHOST, "live", _stream_id, _info.url);
        }
    }
    _muxer->setMediaListener(shared_from_this());

    auto videoTrack = getTrack(TrackVideo, false);
    if (videoTrack) {
        //添加视频
        _muxer->addTrack(videoTrack);
        //视频数据写入_mediaMuxer
        videoTrack->addDelegate(_muxer);
        //视频数据写入帧摄取器
        if (_ingest) {
            videoTrack->addDelegate(_ingest);
        }
    }

    auto audioTrack = getTrack(TrackAudio, false);
    if (audioTrack) {
        //添加音频
        _muxer->addTrack(audioTrack);
        //音频数据写入_mediaMuxer
        audioTrack->addDelegate(_muxer);
        //音频数据写入帧摄取器
        if (_ingest) {
            videoTrack->addDelegate(_ingest);
        }
    }

    //添加完毕所有track，防止单track情况下最大等待3秒
    _muxer->addTrackCompleted();

    if (_media_src) {
        //让_muxer对象拦截一部分事件(比如说录像相关事件)
        _media_src->setListener(_muxer);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////
bool FrameIngest::inputFrame(const Frame::Ptr &frame) {

    weak_ptr<FrameIngest> weak_self = shared_from_this();
    auto flush_video = [weak_self](uint64_t dts, uint64_t pts, const Buffer::Ptr &buffer, bool have_idr) {
        auto strong_self = weak_self.lock();
        if (!strong_self) { return; }

        if (strong_self->_on_data) {
            mgw_packet pkt = {};
            pkt.data = (uint8_t*)buffer->data();
            pkt.size = buffer->size();
            pkt.dts = dts * 1000;
            pkt.pts = pts * 1000;
            pkt.eid = strong_self->_video_eid == CodecH265 ? EncoderId_H265 : EncoderId_H264;
            pkt.type = EncoderType_Video;
            pkt.keyframe = (have_idr/* || frame->keyFrame()*/);
            strong_self->_on_data(&pkt);
        }
    };

    switch (frame->getCodecId()) {
        case CodecH264:
        case CodecH265: {
            if (_video_eid != frame->getCodecId()) {
                _video_eid = frame->getCodecId();
            }
            _merge.inputFrame(frame, flush_video);
            break;
        }
        default: {
            //遇到音频帧，先刷新一下视频帧合并缓存，如果有完整的视频nalu则先输出它
            //不用等到下一帧视频到来才输出视频，否则中间来音频帧会导致buffer时间戳不连续
            //帧的派发，在同一个线程上，不需要考虑多线程的问题
            _merge.inputFrame(nullptr, flush_video);

            mgw_packet pkt = {};
            pkt.data = (uint8_t*)frame->data();
            pkt.size = frame->size();
            pkt.dts = frame->dts() * 1000;
            pkt.pts = frame->pts() * 1000;
            pkt.eid = EncoderId_AAC;
            pkt.type = EncoderType_Audio;
            //先固定成0
            pkt.audio_track = 0;
            pkt.keyframe = frame->keyFrame();
            _on_data(&pkt);
            break;
        }
    }
    return true;
}

}