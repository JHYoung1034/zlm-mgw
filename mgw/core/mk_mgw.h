//为设备使用mgw提供API
#ifndef __MGW_MK_MGW_H__
#define __MGW_MK_MGW_H__

#include "Defines.h"
#include <inttypes.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void handler_t;

/**< Context structure */
typedef struct mgw_context {
    const char      *vendor;    //设备厂商
    const char      *type;      //设备型号
    const char      *sn;        //设备序列号
    const char      *version;   //设备版本号
    uint32_t        max_pusher; //最大推流通道数
    uint32_t        max_player; //最大拉流播放数
    uint32_t        max_bitrate;//非4K流最大允许的码率
    uint32_t        max_4kbitrate;//4k源最大允许的码率
    void (*mgw_callback)(handler_t *h, void *param); //回调通知到设备业务层
}mgw_context;

//定义一些需要回调通知的结构
typedef struct stream_attribute {
    uint32_t        max_pushers;
    uint32_t        max_players;
    uint32_t        max_bitrate;//非4K流最大允许的码率
    uint32_t        max_4kbitrate;//4k源最大允许的码率
}stream_attribute;

typedef struct pusher_status {
    bool                remote;
    uint32_t            channel;
    enum ChannelStatus  status;
    enum ErrorCode      error;
    int                 start_time;
    void                *priv;
}pusher_status;

typedef struct waterline_info {
    uint32_t            channel;
    int                 queue_len;
    int                 threshold;
    int                 frame_cnt;
    int                 send_speed;
}waterline_info;

typedef struct net_info {
    uint16_t        mtu;
    const char      *netif;
}netinfo;

handler_t *mgw_create_device(mgw_context *ctx);
void mgw_release_device(handler_t *h);

/////////////////////////////////////////////////////////////////
typedef struct conn_info {
    int             ka_sec; //心跳保活包发送间隔
    const char      *netif;
    const char      *access_key;
    const char      *url;
}conn_info;

int mgw_connect2server(handler_t *h, const conn_info *info);
void mgw_disconnect4server(handler_t *h);
bool mgw_server_available(handler_t *h);

/////////////////////////////////////////////////////////////////
typedef struct stream_meta {
    enum EncoderId  video_id, audio_id;
    uint16_t        width, height, fps, vkbps;
    uint16_t        channels, samplerate, samplesize, akbps;
}stream_meta;

typedef struct source_info {
    uint32_t        channel;
    bool            local;  //这个字段指定是设备源还是网络(文件)输入源
    union src_info {
        stream_meta local_info;
        struct play_info {
            const char      *url;
            const char      *netif;
            uint16_t        mtu;
            void (*meta_update)(uint32_t channel, stream_meta *info);
            void (*input_packet)(uint32_t channel, mgw_packet *pkt);
        }play_info;
    };
    src_info src;
}source_info;

enum InputType {
    InputType_Raw = 0,
    InputType_play,
    InputType_publish,
};

int mgw_add_source(handler_t *h, source_info *info);
int mgw_input_packet(handler_t *h, uint32_t channel, mgw_packet *pkt);
void mgw_release_source(handler_t *h, uint32_t channel);
void mgw_update_meta(handler_t *h, uint32_t channel, stream_meta *info);
bool mgw_has_source(handler_t *h, uint32_t channel);
void mgw_start_recorder(handler_t *h, uint32_t channel, enum InputType type);
void mgw_stop_recorder(handler_t *h, uint32_t channel, enum InputType type);

/////////////////////////////////////////////////////////////////
typedef struct pusher_info {
    uint32_t        pusher_chn;
    uint32_t        src_chn;
    bool            remote;
    const char      *url;
    const char      *ip;
    const char      *bind_netif;
    uint16_t        bind_mtu;
    const char      *priv;
}pusher_info;

int mgw_add_pusher(handler_t *h, pusher_info *info);
void mgw_release_pusher(handler_t *h, uint32_t chn);
bool mgw_has_pusher(handler_t *h, uint32_t chn, bool remote);

/////////////////////////////////////////////////////////////////
typedef struct play_service_attr {
    bool        stop;
    bool        stop_all;
    bool        local_service;
    uint32_t    players;
    const char  *schema;
    const char  *play_url;
}play_service_attr;

void mgw_set_play_service(handler_t *h, play_service_attr *attr);
void mgw_get_play_service(handler_t *h, play_service_attr *attr);

#ifdef __cplusplus
}
#endif
#endif  //__MGW_MK_MGW_H__