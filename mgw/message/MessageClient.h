//实现websocket client，使用 WebsocketClient
#ifndef __MGW_MSG_CLIENT_H__
#define __MGW_MSG_CLIENT_H__

#include "Util/TimeTicker.h"
#include "Http/WebSocketClient.h"
#include "MessageCodec.h"
#include "core/UcastDevice.h"

namespace mediakit {

class MessageClient : public toolkit::TcpClient, public MessageCodec {
public:
    using Ptr = std::shared_ptr<MessageClient>;

    MessageClient(const toolkit::EventPoller::Ptr &poller = nullptr,
                Entity entity = Entity_Device, const DeviceHelper::Ptr &ptr = nullptr);
    ~MessageClient() override;

    int SendMessage(const char *msg, size_t size);
    int SendMessage(const std::string &msg);

protected:
    void onRecv(const toolkit::Buffer::Ptr &pBuf) override;
    //被动断开连接回调
    void onErr(const toolkit::SockException &ex) override;
    //tcp连接成功后每2秒触发一次该事件
    void onManager() override;
    //连接服务器结果回调
    void onConnect(const toolkit::SockException &ex) override;

    //数据全部发送完毕后回调
    void onFlush() override;

private:
    ////////////////实现MessageParse的接口//////////////////////
    void onMessage(MessagePacket::Ptr msg_pkt) override;
    void onSendRawData(toolkit::Buffer::Ptr buffer) override;

    /////////////////////////////////////////////////////////
    void onProcessCmd(ProtoBufDec &dec);

    void onMsg_sessionRsp(ProtoBufDec &dec);
    void onMsg_commonRsp(ProtoBufDec &dec);
    void onMsg_outputStatus(ProtoBufDec &dec);
    void onMsg_statusRsp(ProtoBufDec &dec);
    void onMsg_startTunnelPush(ProtoBufDec &dec);
    void omMsg_stopTunnelPush(ProtoBufDec &dec);

    //////////////////////////////////////////////////////////
    void onMsg_ServerSessionRsp(ProtoBufDec &dec);

private:
    uint64_t _total_bytes = 0;
    toolkit::Timer::Ptr _ka_timer = nullptr;
    //设备实例，使用弱引用，防止和循环引用
    std::weak_ptr<DeviceHelper> _device_helper;
};

}
#endif  //__MGW_MSG_CLIENT_H__