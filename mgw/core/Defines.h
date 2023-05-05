#ifndef __MGW_DEFINES_H__
#define __MGW_DEFINES_H__

#define TUNNEL_PUSHER "TunnelPuhser"
#define DEFAULT_LOCAL_INPUT "local://localhost/device/"

#include <inttypes.h>
#include <string>

typedef int ChannelId;
typedef int Time_t;

enum EncoderType : uint8_t {
    EncoderType_None = 0,
    EncoderType_Video,
    EncoderType_Audio,
};

enum EncoderId : uint8_t {
    EncoderId_None = 0,
    EncoderId_H264,
    EncoderId_H265,
    EncoderId_AAC,
};

enum ChannelType {
    ChannelType_None = 0,
    ChannelType_Source,
    ChannelType_Output,

    ChannelType_ProxySource,
    ChannelType_ProxyOutput,
};

enum ChannelStatus {
    ChannelStatus_Idle = 0,
    ChannelStatus_Pushing,
    ChannelStatus_RePushing,

    ChannelStatus_Playing,
    ChannelStatus_RePlaying,
};

enum StreamType {
    StreamType_None = 0,
    StreamType_Sock,        // U727内部传输的 domain socket
    StreamType_Dev,         // 从设备拉流
    StreamType_Play,        // 从源地址拉流
    StreamType_Publish,     // 本地监听等待推流
    StreamType_File,        // 从本地文件输入
};

enum ErrorCode {
    Success = 0,        //成功
    Invalid_Param,      //输入参数非法
    Error_Memory,       //内存错误，一般是申请内存出错
    Invalid_Encode,     //不支持的编码
    Error_Thread,       //线程操作异常，比如创建线程失败
    Already_Exist,      //已经存在，重复操作
    Error_Meta,         //错误的Meta信息
    Invalid_Addr,       //非法的地址
    Invalid_Mgw,        //mgw-server不可用
    Invalid_Proto,      //不支持的协议
    Timeout,            //超时
    Blocked,            //阻塞
    Abort,              //异常终止
    Reseted,            //重置，一般是对端关闭了连接
    NotFound,           //找不到主机，一般是对端服务器没开机，或者根本不存在
    NoData,             //没有所请求的数据
    Invalid_Url,        //错误的流地址/码
    Invalid_SSL,        //无用的ssl证书，服务器发来的ssl证书无效
    Common_Failed,      //一般的推/拉流错误
};

struct StreamInfo {
    uint32_t channel;
    uint32_t startTime;
    uint32_t stopTime;
    uint32_t total_retry;
    ChannelStatus status;
    uint64_t totalByteSnd;
    std::string url;
};

typedef struct mgw_packet {
    uint8_t         *data;
    size_t          size;
    int64_t         pts, dts;
    int32_t         tbn, tbd;   //Timebase numerator, Timebase denominator
    enum EncoderType    type;
    enum EncoderId      eid;
    bool            keyframe;
    uint8_t         audio_track;    //音频可能有多个track
}mgw_packet;

//ErrorCode
#define mgw_error(e) -(e)

#endif  //__MGW_DEFINES_H__