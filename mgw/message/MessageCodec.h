//处理消息结构解析
#ifndef __MGW_MESSAGE_CODEC_H__
#define __MGW_MESSAGE_CODEC_H__

#include "Message.h"

namespace mediakit {

//websocket 交付的是完整的消息，不需要继承HttpRequestSplitter去判断消息分片边界
class MessageCodec {

public:
    MessageCodec(Entity entity);
    ~MessageCodec();

    //由裸数据生成 mgw message, 得到消息后，调用onMessage() 函数交由继承者处理
    void onParseMessage(const char *data, size_t size);
    //作为客户端的时候，发起会话请求，等待会话回复后回调处理
    void StartClientSession(const std::function<void()> &cb, bool complex = true);

protected:
    //定义接口，当解析protobuf完成后，调用这个接口，交由继承者处理消息。
    virtual void onMessage(MessagePacket::Ptr msg_pkt) = 0;
    //定义接口，当打包完消息后，调用这个接口，交由继承者处理发送消息。
    virtual void onSendRawData(toolkit::Buffer::Ptr buffer) = 0;

public:
    void sendSession(const std::string &sn, const std::string &dev_type,
                const std::string &version, const std::string &vendor, const std::string &access,
                uint32_t chn_nums = 4, uint32_t max_bitrate = 6144, uint32_t max_bitrate4k = 12288);
    void sendStatusReq(uint32_t src_chn = 0);
    void sendPlayersAttr(bool enable, bool force_stop, const std::string &schema);
    void sendStartProxyPush(uint32_t out_chn, const std::string &url, uint32_t src_chn = 0);
    void sendStopProxyPush(uint32_t out_chn, uint32_t src_chn = 0);
    //mgw-server请求设备把流推送到mgw-server
    void sendStartTunnelPush(uint32_t src_chn = 0, const std::string &url = "");
    //mgw-server请求设备停止推流到mgw-server
    void sendStopTunnelPush(uint32_t src_chn, const std::string &url = "");
    void sendComResponse(CmdType cmd, uint32_t src_chn, uint32_t out_chn,
                            int result = 0, const std::string &des = "");

    /////////////////////////////////////////////////////////////////////////////////////////////////////////
    //发送消息到u727
    void sendDeviceOnline(const std::string &sn, const std::string &type,
                        const std::string &ver, const std::string &ven);
    void sendDeviceOffline(const std::string &sn);
    void sendStreamStatus(const std::string &stream_id, int status, int start_time, int err);
    void sendStreamStarted(const std::string &sn, uint32_t channel, const std::string &url);
    void sendStreamStoped(const std::string &sn, uint32_t channel, const std::string &url);

protected:
    //server --> server
    void sendSvrSession(uint64_t uptime, uint32_t interval,
                        const std::string &host, uint16_t port,
                        const std::string &path, const std::string &key,
                        const std::string &version);

    //发送mgw消息请求，打包mgw消息头和protobuf序列，调用onSendRawData()
    void sendMessage(ProtoBufEnc &enc, bool need_ack);
    void sendRequest(ProtoBufEnc &enc);
    void sendResponse(ProtoBufEnc &enc);

    void setPeerEntity(Entity peer) { _peer_entity = peer; }

private:
    MessageCodec() = delete;
    void getHeader(MsgHeader &header, uint32_t payload_size, bool have_ack, bool have_sub = false);
    uint32_t createSessionId();

private:
    //上一个接收的序列号
    uint32_t _last_rcv_seq = 0;
    //上一个发送的序列号
    uint32_t _last_snd_seq = 0;
    //消息的封装格式，比如protobuf, json字符串，text文本。。。
    PayloadType _payload = PayloadType_Protobuf;
    //实体类型，比如 device，mgw-server，msg-center，agg-server。。。
    Entity _entity, _peer_entity;
    EncryptType _encrypt_type;
    //会话ID，由mgw服务器生成
    uint32_t _session_id;
};

}
#endif  //__MGW_MESSAGE_PARSE_H__