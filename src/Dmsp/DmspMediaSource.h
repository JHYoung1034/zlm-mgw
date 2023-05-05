#ifndef __MGW_CORE_DMSP_MEDIA_SOURCE_H__
#define __MGW_CORE_DMSP_MEDIA_SOURCE_H__

#include <memory>
#include "Dmsp.h"
#include "Common/MediaSource.h"
#include "Common/PacketCache.h"
#include "Util/RingBuffer.h"

#define DMSP_GOP_SIZE 512

namespace mediakit {

class DmspMediaSource : public MediaSource, public toolkit::RingDelegate<DmspPacket::Ptr>, private PacketCache<DmspPacket> {
public:
    using Ptr = std::shared_ptr<DmspMediaSource>;
    using RingDataType = std::shared_ptr<toolkit::List<DmspPacket::Ptr> >;
    using RingType = toolkit::RingBuffer<RingDataType>;

    /**
     * 构造函数
     * @param vhost 虚拟主机名
     * @param app 应用名
     * @param stream_id 流id
     * @param ring_size 可以设置固定的环形缓冲大小，0则自适应
     */
    DmspMediaSource(const std::string &vhost,
                    const std::string &app,
                    const std::string &stream_id,
                    int ring_size = DMSP_GOP_SIZE) :
            MediaSource(DMSP_SCHEMA, vhost, app, stream_id), _ring_size(ring_size) {
    }

    ~DmspMediaSource() override { flush(); }

    /**
     * 	获取媒体源的环形缓冲
     */
    const RingType::Ptr &getRing() const {
        return _ring;
    }

    void getPlayerList(const std::function<void(const std::list<std::shared_ptr<void>> &info_list)> &cb,
                       const std::function<std::shared_ptr<void>(std::shared_ptr<void> &&info)> &on_change) override {
        _ring->getInfoList(cb, on_change);
    }

    /**
     * 获取播放器个数
     * @return
     */
    int readerCount() override {
        return _ring ? _ring->readerCount() : 0;
    }

    /**
     * 获取metadata
     */
    // const AMFValue &getMetaData() const {
    //     std::lock_guard<std::recursive_mutex> lock(_mtx);
    //     return _metadata;
    // }

    /**
     * 获取所有的config帧
     */
    template<typename FUNC>
    void getConfigFrame(const FUNC &f) {
        std::lock_guard<std::recursive_mutex> lock(_mtx);
        for (auto &pr : _config_frame_map) {
            f(pr.second);
        }
    }

    /**
     * 设置metadata
     */
    // virtual void setMetaData(const AMFValue &metadata) {
    //     _metadata = metadata;
    //     _metadata.set("server", kServerName);
    //     _have_video = _metadata["videocodecid"];
    //     _have_audio = _metadata["audiocodecid"];
    //     if (_ring) {
    //         regist();
    //     }
    // }

    /**
     * 更新metadata
     */
    // void updateMetaData(const AMFValue &metadata) {
    //     std::lock_guard<std::recursive_mutex> lock(_mtx);
    //     _metadata = metadata;
    // }

    /**
     * 输入dmsp包
     * @param pkt dmsp包
     */
    void onWrite(DmspPacket::Ptr pkt, bool = true) override {
        bool is_video = pkt->info.track.type == EncoderType_Video;
        _speed[is_video ? TrackVideo : TrackAudio] += pkt->size(); //仅仅是音视频净负载

        switch (pkt->info.track.type) {
            case EncoderType_Video: _track_stamps[TrackVideo] = pkt->info.timestamp, _have_video = true; break;
            case EncoderType_Audio: _track_stamps[TrackAudio] = pkt->info.timestamp, _have_audio = true; break;
            default: break;
        }

        //如果是第一个数据包到来，要创建ring buffer，同时要注册到源全局容器中(调用register() )
        if (!_ring) {
            std::weak_ptr<DmspMediaSource> wek_self = std::dynamic_pointer_cast<DmspMediaSource>(shared_from_this());
            auto on_reader_changed = [wek_self](int size) {
                auto strong_self = wek_self.lock();
                if (!strong_self) { return; }

                strong_self->onReaderChanged(size);
            };

            _ring = std::make_shared<RingType>(_ring_size, std::move(on_reader_changed));
            regist();
        }

        bool key = pkt->isVideoKeyFrame();
        auto ts = pkt->info.timestamp;
        PacketCache<DmspPacket>::inputPacket(ts, is_video, std::move(pkt), key);
    }

    /**
     * 获取当前时间戳
     */
    uint32_t getTimeStamp(TrackType trackType) override {
        return _track_stamps[trackType];
    }

    void clearCache() override{
        PacketCache<DmspPacket>::clearCache();
        _ring->clearCache();
    }

    bool haveVideo() const {
        return _have_video;
    }

    bool haveAudio() const {
        return _have_audio;
    }

private:
    /**
    * 批量flush dmsp包时触发该函数
    * @param dmsp_list dmsp包列表
    * @param key_pos 是否包含关键帧
    */
    void onFlush(std::shared_ptr<toolkit::List<DmspPacket::Ptr> > dmsp_list, bool key_pos) override {
        //如果不存在视频，那么就没有存在GOP缓存的意义，所以is_key一直为true确保一直清空GOP缓存
        _ring->write(std::move(dmsp_list), _have_video ? key_pos : true);
    }

private:
    bool _have_video = false;
    bool _have_audio = false;
    int _ring_size;
    uint32_t _track_stamps[TrackMax] = {0};
    // AMFValue _metadata;
    RingType::Ptr _ring;

    mutable std::recursive_mutex _mtx;
    std::unordered_map<int, DmspPacket::Ptr> _config_frame_map;

};

}
#endif  //__MGW_CORE_DMSP_MEDIA_SOURCE_H__