#ifndef __MGW_MESSAGE_H__
#define __MGW_MESSAGE_H__

#include <cinttypes>
#include <arpa/inet.h>
#include "Network/Buffer.h"

#define MAGIC_CODE	0x66886688
#define MESSAGE_VER	2

#if !defined(_WIN32)
#define PACKED	__attribute__((packed))
#else
#define PACKED
#endif //!defined(_WIN32)

namespace mgw {
    class MgwMsg;
}

/** ---------------------------------------------------------------------------------------*/
namespace MGW {
enum CmdType {
    /** Device-->Server request message */
    CmdType_None        = 0,
    CmdType_SessionReq,
    CmdType_StartPush,
    CmdType_StopPush,
    CmdType_StatusReq,
    CmdType_QuerySrc,
    CmdType_SetPlayAttr,

    /** Server-->Device response message */
    CmdType_SessionRes,
    CmdType_CommonRes,
    CmdType_StopTunnel,
    CmdType_PushResult,
    CmdType_StatusRes,
    CmdType_StartTunnel,

    //server --> server
    CmdType_SvrSessionReq,
    CmdType_SvrSessionRes,

    /** For internal */
    CmdType_ReleaseDev,
    CmdType_WsPing,
};

struct ExtendHeader {
    uint8_t     subPtotoType;
    uint8_t     subHeaderSize;
    //exten message is TBD
};

enum PayloadType {
    PayloadType_None       = 0,
    PayloadType_Protobuf   = 1,
    PayloadType_Json       = 2,
    PayloadType_Text       = 3,
    /** 4-200 reserve */
    /** 201-220 Customized */
    /** 221-255 forbidden */
};

enum Entity {
    Entity_Device      = 0,
    Entity_MsgCenter   = 1,
    Entity_MgwServer   = 2,
    Entity_AggServer   = 3,    //aggregation server
    Entity_MgwMaster   = 4
};

enum EncryptType {
    EncryptType_None      = 0,
    EncryptType_Aes256    = 1,
    /** reserve */
};

/** ---------------------------------------------------------------------*/
/** ws information */
enum WsStatus {
	WsStatus_Success    = 0,
	WsStatus_EParam     = -1,
	WsStatus_ELoop      = -2,
	WsStatus_EParse     = -3,
    WsStatus_EActive    = -4,
    WsStatus_ERepeated  = -5,
};

class MsgHeader {
public:
    uint32_t    _magic;        //MAGIC_CODE
    uint8_t    _version;      //must be 2
    uint8_t    _headerSize;   //include the sub header length
    uint16_t    _payloadSize;
    uint32_t    _msgSeq;    //发送方的发送的消息序号，每个消息+1

    uint8_t     _protoType;     //PayloadType_Protobuf
    uint8_t     _senderPid;     //Entity
    uint8_t     _receiverPid;   //Entity
#if __BYTE_ORDER == __BIG_ENDIAN
    uint8_t     _haveSubProto:1;
    uint8_t     _needAck:1;
    uint8_t     _rsv:2;
    uint8_t     _encryptType:4;
#else
    uint8_t    _encryptType:4;  //EncryptType
    uint8_t    _rsv:2;
    uint8_t    _needAck:1;
    uint8_t    _haveSubProto:1;
#endif
    uint32_t    _ackSeq;        //指定回复某一条消息，一般是上次接收到到的消息序号
    uint32_t    _sessionId;
    /** extend header */
    // uint8_t     _subPtotoType;
    // uint8_t     _subHeaderSize;
}PACKED;

//此类用于检查mgw控制消息，一般是对MsgHeader做合法性检查
class MessagePacket : public toolkit::Buffer {
public:
    using Ptr = std::shared_ptr<MessagePacket>;
    toolkit::BufferLikeString _buffer;
    CmdType _cmd = CmdType_None;
    int _sequence = 0;
    bool _need_ack = false;

public:
    static Ptr create() {
        return Ptr(new MessagePacket());
    }

    char *data() const override {
        return (char *)_buffer.data();
    }
    size_t size() const override {
        return _buffer.size();
    }

    void clear() {
        _buffer.clear();
        _cmd = CmdType_None;
    }

    /** shallow copy */
    MessagePacket &operator=(const MessagePacket &that) {
        _cmd = that._cmd;
        return (*this);
    }
private:
    friend class toolkit::ResourcePool_l<MessagePacket>;
    MessagePacket() {
        clear();
    }

// private:
//     toolkit::ObjectStatistic<MessagePacket> _statistic;
};

/**
 * 此类用于包装protobuf消息对象，
 * 1.可由裸数据生成protobuf消息对象。
 * 2.可load()获取是何种protobuf消息。
 * 3.message获取到的protobuf消息对象，调用者可通过cast转换成具体的消息。
 * 4.对于消息的操作由调用者完成。
*/
using MsgPtr = std::shared_ptr<mgw::MgwMsg>;
class ProtoBufDec {
public:
    explicit ProtoBufDec(const toolkit::BufferLikeString &buf);

    MsgPtr messge() const { return _msg; }
    //load the message method
    std::string load();
    std::string toString() const;

private:
    ProtoBufDec() = delete;
    MsgPtr _msg;
};

//序列化protobuf消息，生成BufferLikeString对象
class ProtoBufEnc {
public:
    explicit ProtoBufEnc(const MsgPtr &msg);
    toolkit::BufferLikeString::Ptr serialize();

private:
    ProtoBufEnc() = delete;
    MsgPtr _msg;
};

}
#endif  //__MGW_MESSAGE_H__