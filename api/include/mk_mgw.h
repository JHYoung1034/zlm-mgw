//为设备使用mgw提供API
#ifndef __MGW_MK_MGW_H__
#define __MGW_MK_MGW_H__

#include <inttypes.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void mgw_handler_t;
#define ENABLE_RTMP_HEVC    1
#define MK_VIDEO_TRACK      0
#define MK_AUDIO_TRACK      1

//定义一些需要回调通知的结构
typedef struct stream_attrs {
    uint32_t        max_pushers;    //最大推流通道数
    uint32_t        max_players;    //最大拉流播放数
    uint32_t        max_bitrate;//非4K流最大允许的码率
    uint32_t        max_4kbitrate;//4k源最大允许的码率
}stream_attrs;

typedef enum mk_status {
    MK_STATUS_IDLE = 0,
    MK_STATUS_PUSHING,
    MK_STATUS_REPUSHING,

    MK_STATUS_PLAYING,
    MK_STATUS_REPLAYING,
}mk_status;

typedef struct status_info {
    bool                remote;
    uint32_t            channel;
    mk_status           status;
    int                 error;      //enum ErrorCode
    int                 start_time;
    void                *priv;
}status_info;

typedef struct waterline_info {
    uint32_t            channel;
    int                 queue_len;
    int                 threshold;
    int                 frame_cnt;
    int                 send_speed;
}waterline_info;

typedef struct net_info {
    uint16_t  mtu;
    char      netif[32];
}netinfo;

typedef enum mk_event {
    MK_EVENT_NONE = 0,
    MK_EVENT_GET_NETINFO = 1,   //netifo
    MK_EVENT_SET_STREAM_CFG,    //stream_attrs
    MK_EVENT_SET_STREAM_STATUS, //status_info
    MK_EVENT_SET_WATERLINE,     //waterline_info
    MK_EVENT_GET_STREAM_META,   //stream_meta
    MK_EVENT_SET_START_VIDEO,   //int channel
    MK_EVENT_SET_STOP_VIDEO,    //int channel
}mk_event_t;

/**< Context structure */
typedef void (*mgw_callback)(mgw_handler_t *h, mk_event_t e, void *param); //回调通知到设备业务层
typedef struct mgw_context {
    const char      *vendor;    //设备厂商
    const char      *type;      //设备型号
    const char      *sn;        //设备序列号
    const char      *version;   //设备版本号
    stream_attrs    stream_attr;
    const char      *log_path;  //日志文件路径
    uint32_t        log_level;  //0-trace, 1-debug, 2-info, 3-warn, 4-error
    mgw_callback    callback;
}mgw_context;

mgw_handler_t *mgw_create_device(mgw_context *ctx);
void mgw_release_device(mgw_handler_t *h);

/////////////////////////////////////////////////////////////////
typedef struct conn_info {
    uint16_t        ka_sec; //心跳保活包发送间隔
    uint16_t        mtu;
    const char      *netif;
    const char      *url;
    const char      *token;     //接入token
}conn_info;

int mgw_connect2server(mgw_handler_t *h, const conn_info *info);
void mgw_disconnect4server(mgw_handler_t *h);
bool mgw_server_available(mgw_handler_t *h);

/////////////////////////////////////////////////////////////////
typedef enum mk_codec_id {
    MK_CODEC_ID_NONE = 0,
    MK_CODEC_ID_H264,
    MK_CODEC_ID_H265,
    MK_CODEC_ID_AAC,
}mk_codec_id;

typedef enum DevInputType {
    INPUT_TYPE_NONE = 0,
    INPUT_TYPE_PHY,     //物理输入
    INPUT_TYPE_PLA,     //拉流输入
    INPUT_TYPE_REC,     //录像输入
    INPUT_TYPE_PUB,     //推流输入
}input_type_t;

typedef struct video_info {
    uint16_t        width;
    uint16_t        height;
    uint16_t        fps;
    uint16_t        vkbps;
}video_info;

typedef struct audio_info {
    uint16_t        channels;
    uint16_t        samplerate;
    uint16_t        samplesize;
    uint16_t        akbps;
}audio_info;

typedef struct track_info {
    mk_codec_id     id;
    union {
        video_info  video;
        audio_info  audio;
    };
}track_info;

typedef struct mk_frame {
    mk_codec_id id;
    const char *data;
    uint32_t size;
    uint64_t dts;   //单位：ms
    uint64_t pts;   //单位：ms
    bool keyframe;
}mk_frame_t;

typedef struct play_info {
    const char      *url;
    const char      *netif;
    uint16_t        mtu;
    void (*meta_update)(uint32_t channel, track_info info);
    void (*input_packet)(uint32_t channel, mk_frame_t frame);
}play_info;

/**在输入数据包的时候，动态生成音视频track*/
int mgw_input_packet(mgw_handler_t *h, uint32_t channel, mk_frame_t frame);
void mgw_release_source(mgw_handler_t *h, bool local, uint32_t channel);
//只有local source才能更新meta
void mgw_update_meta(mgw_handler_t *h, uint32_t channel, track_info info);
bool mgw_have_raw_video(mgw_handler_t *h, uint32_t channel);
bool mgw_have_raw_audio(mgw_handler_t *h, uint32_t channel);
//开启录像
void mgw_start_recorder(mgw_handler_t *h, bool local, input_type_t it, uint32_t channel);
void mgw_stop_recorder(mgw_handler_t *h, bool local, input_type_t it, uint32_t channel);

/////////////////////////////////////////////////////////////////
typedef struct pusher_info {
    bool            remote;
    uint32_t        pusher_chn;
    bool            src_remote;
    input_type_t    src_type;
    uint32_t        src_chn;
    const char      *url;
    const char      *key;
    const char      *username;
    const char      *password;
    const char      *ip;
    const char      *bind_netif;
    uint16_t        bind_mtu;
    void            *user_data;
}pusher_info;

int mgw_add_pusher(mgw_handler_t *h, pusher_info *info);
void mgw_release_pusher(mgw_handler_t *h, bool remote, uint32_t chn);
bool mgw_has_pusher(mgw_handler_t *h, bool remote, uint32_t chn);
bool mgw_has_tunnel_pusher(mgw_handler_t *h);
void mgw_release_tunnel_pusher(mgw_handler_t *h);

/////////////////////////////////////////////////////////////////
//播放服务属性
typedef struct play_service_attr {
    bool        stop;
    bool        stop_all;
    bool        local_service;
    int         src_chn;
    uint32_t    players;
    const char  *schema;
    const char  *play_url;
}play_service_attr;

void mgw_set_play_service(mgw_handler_t *h, play_service_attr *attr);
void mgw_get_play_service(mgw_handler_t *h, play_service_attr *attr);

/////////////////////////////////////////////////////////////////
typedef void (*on_status)(uint32_t channel, status_info info);
typedef void (*on_data)(uint32_t channel, mk_frame_t frame);
typedef void (*on_meta)(uint32_t channel, input_type_t it, track_info info);

typedef struct player_attr {
    bool        remote;
    int         channel;
    input_type_t it;
    const char  *url;
    const char  *netif;
    uint16_t    mtu;

    on_status   status_cb;
    on_data     data_cb;
    on_meta     meta_cb;
}player_attr;

void mgw_add_player(mgw_handler_t *h, player_attr attr);
void mgw_release_player(mgw_handler_t *h, bool remote, input_type_t it, int channel);

#ifdef __cplusplus
}
#endif
#endif  //__MGW_MK_MGW_H__