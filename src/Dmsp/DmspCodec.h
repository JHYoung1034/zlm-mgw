/**< Domain Socket Protocol, 一个基于Unix域套接字的，自定义的，
 * 在同一台机器上使用的，流媒体传输协议，不走TCP/IP协议栈，
 * 纯虚拟文件系统传输，简单，高效 */

#ifndef __MGW_CORE_DMSP_CODEC_H__
#define __MGW_CORE_DMSP_CODEC_H__

#include "Dmsp/Dmsp.h"
#include "Extension/Frame.h"
#include "Util/RingBuffer.h"

namespace mediakit{

class DmspRing {
public:
    using Ptr = std::shared_ptr<DmspRing>;
    using RingType = toolkit::RingBuffer<DmspPacket::Ptr>;

    DmspRing() = default;
    virtual ~DmspRing() = default;

    /**
     * 获取dmsp环形缓存
     */
    virtual RingType::Ptr getDmspRing() const {
        return _ring;
    }

    /**
     * 设置dmsp环形缓存
     */
    virtual void setDmspRing(const RingType::Ptr &ring) {
        _ring = ring;
    }

    /**
     * 输入dmsp包
     * @param dmsp dmsp包
     */
    virtual void inputDmsp(const DmspPacket::Ptr &dmsp) {
        if (_ring) {
            _ring->write(dmsp, dmsp->isVideoKeyFrame());
        }
    }

protected:
    RingType::Ptr _ring;
};

//如果继承者是muxer，那么mux之后得到DmspPacket会调用DmspRing::inputDmsp
//如果继承者是demuxer, 那么demux之后得到的frame音视频帧会调用FrameDispatcher::inputFrame
class DmspCodec : public DmspRing, public FrameDispatcher, public CodecInfo {
public:
    using Ptr = std::shared_ptr<DmspCodec>;
    DmspCodec() = default;
    ~DmspCodec() override = default;
    virtual void makeConfigPacket() {};
};


}//namespace mediakit
#endif  //__MGW_CORE_DMSP_CODEC_H__