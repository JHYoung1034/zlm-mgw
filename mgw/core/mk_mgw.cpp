#include "mk_mgw.h"
#include "UcastDevice.h"
#include "message/MessageClient.h"
#include "Util/onceToken.h"

#include <assert.h>

using namespace std;
using namespace toolkit;
using namespace mediakit;

class DeviceHandle {
public:
    using Ptr = shared_ptr<DeviceHandle>;

    DeviceHandle(mgw_context *ctx);
    ~DeviceHandle();

private:
    EventPoller::Ptr   _poller = nullptr;
    DeviceHelper::Ptr  _device_helper = nullptr;
    MessageClient::Ptr _ws_client = nullptr;
};

DeviceHandle::DeviceHandle(mgw_context *ctx) {

    //初始化日志
    //初始化配置
    //初始化线程池
    //初始化协议服务

    _poller = EventPollerPool::Instance().getPoller();
    _device_helper = make_shared<DeviceHelper>(ctx->sn, _poller);
    _ws_client = make_shared<MessageClient>(_poller, Entity_Device, _device_helper);
}

///////////////////////////////////////////////////////////////////////////

handler_t *mgw_create_device(mgw_context *ctx) {
    assert(ctx);
    DeviceHandle::Ptr *obj(new DeviceHandle::Ptr(new DeviceHandle(ctx)));
    return (handler_t*)obj;
}

void mgw_release_device(handler_t *h) {
    assert(h);
    DeviceHandle::Ptr *obj = (DeviceHandle::Ptr *) h;
    delete obj;
}

///////////////////////////////////////////////////////////////////////////
int mgw_connect2server(handler_t *h, const conn_info *info) {

    return 0;
}

void mgw_disconnect4server(handler_t *h) {

}

bool mgw_server_available(handler_t *h) {

    return true;
}

///////////////////////////////////////////////////////////////////////////
int mgw_add_source(handler_t *h, source_info *info) {

    return 0;
}

int mgw_input_packet(handler_t *h, uint32_t channel, mgw_packet *pkt) {

    return 0;
}

void mgw_release_source(handler_t *h, uint32_t channel) {

}

void mgw_update_meta(handler_t *h, uint32_t channel, stream_meta *info) {

}

bool mgw_has_source(handler_t *h, uint32_t channel) {

    return true;
}

void mgw_start_recorder(handler_t *h, uint32_t channel, enum InputType type) {

}

void mgw_stop_recorder(handler_t *h, uint32_t channel, enum InputType type) {

}

///////////////////////////////////////////////////////////////////////////
int mgw_add_pusher(handler_t *h, pusher_info *info) {


    return 0;
}

void mgw_release_pusher(handler_t *h, uint32_t chn) {

}

bool mgw_has_pusher(handler_t *h, uint32_t chn, bool remote) {

    return false;
}

///////////////////////////////////////////////////////////////////////////
void mgw_set_play_service(handler_t *h, play_service_attr *attr) {

}

void mgw_get_play_service(handler_t *h, play_service_attr *attr) {

}

///////////////////////////////////////////////////////////////////////////
void mgw_add_player(handler_t *h, const player_attr *attr) {

}

void mgw_release_player(handler_t *h, int channel) {

}
