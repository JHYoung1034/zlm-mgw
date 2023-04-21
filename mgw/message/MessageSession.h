//实现mgw-server与设备的消息会话处理
#ifndef __MGW_CORE_MESSAGE_SESSION_H__
#define __MGW_CORE_MESSAGE_SESSION_H__

#include "Http/WebSocketSession.h"
#include "MessageCodec.h"
#include "core/UcastDevice.h"
#include "core/U727.h"

namespace mediakit {

class DeviceSession : public toolkit::Session, public MessageCodec {
public:
    using Ptr = std::shared_ptr<DeviceSession>;
    DeviceSession(const toolkit::Socket::Ptr &sock);
    ~DeviceSession() override;

    //用于把服务器属性下发给新到来的连接会话
    // void attachServer(const toolkit::Server &server);
    //
    void onRecv(const toolkit::Buffer::Ptr &buffer) override;
    void onError(const toolkit::SockException &err) override;
    //每隔一段时间触发，用来做超时管理
    void onManager() override;

protected:
    //得到mgw message，解析出protobuf
    void onMessage(MessagePacket::Ptr msg_pkt) override;
    void onSendRawData(toolkit::Buffer::Ptr buffer) override;

private:
    void onProcessCmd(ProtoBufDec &dec);
    void onMsg_sessionReq(ProtoBufDec &dec);
    void onMsg_startProxyPush(ProtoBufDec &dec);
    void onMsg_stopProxyPush(ProtoBufDec &dec);
    void onMsg_statusReq(ProtoBufDec &dec);
    void onMsg_setPlayAttr(ProtoBufDec &dec);

    ///////////////////////////////////////////////
    void setEventHandle();

private:
    uint32_t _total_bytes = 0;
    //数据接收超时计时器
    toolkit::Ticker _ticker;
    //设备实例
    DeviceHelper::Ptr _device_helper;
};

class U727Session : public toolkit::Session, public MessageCodec {
public:
    using Ptr = std::shared_ptr<U727Session>;
    U727Session(const toolkit::Socket::Ptr &sock);
    ~U727Session() override;

    //用于把服务器属性下发给新到来的连接会话
    // void attachServer(const toolkit::Server &server);
    //
    void onRecv(const toolkit::Buffer::Ptr &buffer) override;
    void onError(const toolkit::SockException &err) override;
    //每隔一段时间触发，用来做超时管理
    void onManager() override;

protected:
    //得到mgw message，解析出protobuf
    void onMessage(MessagePacket::Ptr msg_pkt) override;
    void onSendRawData(toolkit::Buffer::Ptr buffer) override;

private:
    void onProcessCmd(ProtoBufDec &dec);
    void onMsg_setBlackList(ProtoBufDec &dec);
    void onMsg_setSrvPort(ProtoBufDec &dec);
    void onMsg_getSrvPort(ProtoBufDec &dec);
    void onMsg_startStreamReq(ProtoBufDec &dec);
    void onMsg_stopStream(ProtoBufDec &dec);
    void onMsg_u727KeepAlive(ProtoBufDec &dec);
    void onMsg_queryOnlineDevReq(ProtoBufDec &dec);

    ///////////////////////////////////////////////
    void setEventHandle();

private:
    uint32_t _total_bytes = 0;
    //数据接收超时计时器
    toolkit::Ticker _ticker;
    //u727实例
    U727::Ptr _u727 = nullptr;
};

/**
 * 此对象可以根据websocket 客户端访问的url选择创建不同的对象
 */
struct MessageSessionCreator {
    //返回的Session必须派生于SendInterceptor，可以返回null(拒绝连接)
    toolkit::Session::Ptr operator()(const mediakit::Parser &header,
                                const mediakit::HttpSession &parent,
                                const toolkit::Socket::Ptr &pSock);
};

}
#endif  //__MGW_CORE_MESSAGE_SESSION_H__