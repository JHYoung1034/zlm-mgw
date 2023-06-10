#include "PlayHelper.h"
#include "Common/macros.h"
#include "Rtmp/RtmpMediaSource.h"
#include "Rtsp/RtspMediaSource.h"
#include "Rtmp/RtmpPlayer.h"
#include "Rtsp/RtspPlayer.h"

#include <strstream>

using namespace std;
using namespace toolkit;

namespace mediakit {

PlayHelper::PlayHelper(const string &name, int chn, int max_retry) {
    _info.channel = chn == -1 ? getOutputChn(name) : chn;
    _max_retry = max_retry;
    _stream_id = _info.id = name;
}

PlayHelper::~PlayHelper() {
    _timer.reset();
    if (_info.status != ChannelStatus_Idle) {
        teardown();
    }
    if (_on_status_changed) {
        _on_status_changed(_info.id, ChannelStatus_Idle, ::time(NULL), SockException());
        _on_status_changed = nullptr;
    }
}

void PlayHelper::setNetif(const string &netif, uint16_t mtu) {
    MediaPlayer::setNetif(netif, mtu);
}

void PlayHelper::start(const string &url, onStatusChanged on_status_changed, onData on_data, onMeta on_meta) {
    _info.url = url;
    _on_status_changed = on_status_changed;
    _on_meta = on_meta;
    _info.startTime = ::time(NULL);

    if (_stream_id.empty()) {
        //创建实例的时候没有穿id，url最右边'/'之后的名字作为id
        _info.id = _stream_id = FindField(url.data(), url.substr(url.rfind('/')+1).data(), NULL);
    }

    if (on_data && !_ingest) {
        _ingest = make_shared<FrameIngest>(on_data, on_meta);
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
void PlayHelper::restart(const string &url, onStatusChanged on_status_changed, onData on_data, onMeta on_meta) {
    if (_info.status != ChannelStatus_Idle) {
        teardown();
    }
    start(url, on_status_changed, on_data, on_meta);
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

        WarnL << "play err: " << err.what();
        //主动关闭不需要重连
        if (err && err.getErrCode() == Err_shutdown) {
            return;
        }

        if ((*failed_cnt) >= 0 && (*failed_cnt < strong_self->_max_retry || strong_self->_max_retry < 0)) {
            /**
             * (*failed_cnt) >= 0 说明有过成功拉流
             * _max_retry 小于0，或者failed_cnt小于_max_retry才可以重新拉流
             */
            strong_self->rePlay(url, ++(*failed_cnt));
        } else {
            //发生了错误，并且没办法重新拉流了，回调通知
            strong_self->_info.stopTime = ::time(NULL);
            strong_self->_info.status = ChannelStatus_Idle;
            if (strong_self->_on_status_changed) {
                strong_self->_on_status_changed(strong_self->_stream_id,
                    strong_self->_info.status, strong_self->_info.startTime, err);
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
            WarnL << "on play result: " << err.what();
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
        if (strong_self->_on_status_changed) {
            DebugL << "Play Result: [" << err.getErrCode() << "]";
            strong_self->_on_status_changed(strong_self->_stream_id, strong_self->_info.status,
                        strong_self->_info.startTime, err);
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
    weak_ptr<PlayHelper> weak_self = shared_from_this();
    _info.status = ChannelStatus_RePlaying;
    if (_on_status_changed) {
        _on_status_changed(_stream_id, _info.status, _info.startTime, SockException(Err_success, "RePlaying", 0));
    }
    _timer = std::make_shared<Timer>(iDelay / 1000.0f, [weak_self, url, failed_cnt]() {
        //播放失败次数越多，则延时越长
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->_info.total_retry++;
        WarnL << "重试播放[" << failed_cnt << "]:" << url;
        strong_self->MediaPlayer::play(url);
        strong_self->setDirectProxy();
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
    if (_on_status_changed) {
        _on_status_changed(_stream_id, ChannelStatus_Idle, _info.startTime, SockException());
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
            _media_src = MediaSource::createFromMP4("rtsp", DEFAULT_VHOST, "live", _stream_id, _info.url);
            if (!_media_src) {
                //从录像文件输入失败了，需要通知
                _info.status = ChannelStatus_Idle;
                if (_on_status_changed) {
                    _on_status_changed(_stream_id, _info.status, _info.startTime,
                                SockException(Err_other, "Create from MP4 failed", -1));
                }
                return;
            }
            _info.status = ChannelStatus_Playing;
            _on_status_changed(_stream_id, _info.status, _info.startTime,
                                SockException(Err_success, "Success", 0));
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
        //发送meta信息
        if (_on_meta) {
            auto video = dynamic_pointer_cast<VideoTrack>(videoTrack);
            //codecid, width, height, fps, vkbps
            _on_meta(videoTrack->getCodecId(), video->getVideoWidth(),
                video->getVideoHeight(), video->getVideoFps(), video->getBitRate());
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
            audioTrack->addDelegate(_ingest);
        }
        //发送meta信息
        if (_on_meta) {
            auto audio = dynamic_pointer_cast<AudioTrack>(audioTrack);
            //codecid, channels, samplerate, samplesize, akbps
            _on_meta(audio->getCodecId(), audio->getAudioChannel(), audio->getAudioSampleRate(),
                            audio->getAudioSampleBit(),audio->getBitRate());
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
        if (!strong_self) {
            return;
        }
        if (strong_self->_on_data) {
            strong_self->_on_data(strong_self->_video_eid, buffer->data(), buffer->size(), dts, pts, have_idr);
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
            //CodecId, const char*, uint32_t, uint64_t, uint64_t, bool
            if (_on_data) {
                _on_data(frame->getCodecId(), frame->data(), frame->size(), frame->dts(), frame->pts(), frame->keyFrame());
            }
            break;
        }
    }
    return true;
}

}