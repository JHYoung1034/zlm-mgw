#ifndef __SRC_DMSP_MEDIA_SOURCE_MUXER_H__
#define __SRC_DMSP_MEDIA_SOURCE_MUXER_H__

#include "DmspMediaSource.h"
#include "DmspMuxer.h"

namespace mediakit {

class DmspMediaSourceMuxer final : public DmspMuxer, public MediaSourceEventInterceptor,
                                   public std::enable_shared_from_this<DmspMediaSourceMuxer> {
public:
    using Ptr = std::shared_ptr<DmspMediaSourceMuxer>;

    DmspMediaSourceMuxer(const std::string &vhost,
                       const std::string &app,
                       const std::string &stream_id,
                       const ProtocolOption &option) {
        _option = option;
        _media_src = std::make_shared<DmspMediaSource>(vhost, app, stream_id);
        getDmspRing()->setDelegate(_media_src);
    }
    ~DmspMediaSourceMuxer() override { DmspMuxer::flush(); }

    void setListener(const std::weak_ptr<MediaSourceEvent> &listener){
        setDelegate(listener);
        _media_src->setListener(shared_from_this());
    }

    void setTimeStamp(uint32_t stamp){
        _media_src->setTimeStamp(stamp);
    }

    int readerCount() const{
        return _media_src->readerCount();
    }

    void onAllTrackReady(){
        //1.所有的track都准备就绪了，生成音视频元信息
        makeConfigPacket();
        //创建媒体的缓存环，同时必须要调用regist()把媒体源注册到全局源容器中
        // _media_src->setMetaData(getMetadata());
    }

    void onReaderChanged(MediaSource &sender, int size) override {
        _enabled = _option.dmsp_demand ? size : true;
        if (!size && _option.dmsp_demand) {
            _clear_cache = true;
        }
        MediaSourceEventInterceptor::onReaderChanged(sender, size);
    }

    bool inputFrame(const Frame::Ptr &frame) override {
        if (_clear_cache && _option.dmsp_demand) {
            _clear_cache = false;
            _media_src->clearCache();
        }
        if (_enabled || !_option.dmsp_demand) {
            return DmspMuxer::inputFrame(frame);
        }
        return false;
    }

    bool isEnabled() {
        //缓存尚未清空时，还允许触发inputFrame函数，以便及时清空缓存
        return _option.dmsp_demand ? (_clear_cache ? true : _enabled) : true;
    }

private:
    bool _enabled = true;
    bool _clear_cache = false;
    ProtocolOption _option;
    DmspMediaSource::Ptr _media_src;
};

}
#endif  //__SRC_DMSP_MEDIA_SOURCE_MUXER_H__