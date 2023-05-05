#ifndef __MGW_CORE_DMSP_SPLITTER_H__
#define __MGW_CORE_DMSP_SPLITTER_H__

#include "Dmsp.h"
#include "Http/HttpRequestSplitter.h"

namespace mediakit {

class DmspSplitter : public HttpRequestSplitter {
public:
    DmspSplitter(){}
    ~DmspSplitter(){}

    void onParseDmsp(const char *data, size_t size);

protected:
    void sendDmsp(const DmspPacket::Ptr &packet);

    /** 定义接口，由继承者实现 */
    /**接收一个完整的DmspPacket完成时调用这个接口，交给继承者处理 */
    virtual void onWholeDmspPacket(DmspPacket::Ptr packet) = 0;
    /** 收到的tack信息出现变化时，调用这个接口，交给继承者处理 */
    virtual void onMetaChange(DmspTack tack) = 0;
    /** */
    virtual void onSendRawData(toolkit::Buffer::Ptr buffer) = 0;

    /**实现HttpRequestSplitter虚函数*/
    virtual ssize_t onRecvHeader(const char *data,size_t len) override;
    const char *onSearchPacketTail(const char *data, size_t len) override;

private:
    toolkit::BufferRaw::Ptr obtainBuffer(const void *data, size_t len);

private:
    //循环池
    toolkit::ResourcePool<toolkit::BufferRaw> _packet_pool;
};

}
#endif  //__MGW_CORE_DMSP_SPLITTER_H__