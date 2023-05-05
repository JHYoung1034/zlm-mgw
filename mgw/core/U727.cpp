#include "U727.h"
#include "Common/config.h"
#include "UcastDevice.h"

using namespace std;

/**< u727实例必须和u727消息会话绑定在同一个线程上，在其他地方调用u727实例，
 *   比如说设备调用发送通知的时候，必须切换到u727绑定的线程再发送通知，
 *   这样就可以确保线程安全。
 * 
 *   这里存在一个问题，如果u727会话频繁断开又重连上来，就可能会导致u727频繁的
 *   切换绑定到不同的线程，假如上一次绑定的线程调用任务还没结束，就会出现问题。
 *   所以，u727实例还是需要跟随u727会话，如果会话断开的时候，不允许其他实例
 *   调用u727。
 * 
 *   基于上述情况，设备实例有多个，u727实例只有一个，而且是应该跟随u727会话的。
 *   所以，设备实例很难强关联到u727实例，很难直接调用。所以处理这种情况下设备实例
 *   对u727实例的单向调用选择消息通知方式。而u727调用设备实例可以直接查找后调用。
*/

namespace mediakit {

void U727::startStream(const string &id, uint32_t delay_ms, StreamType src_type,
            const string &src, StreamType dest_type, const string &dest) {

    //1. 查找源，如果找不到，就请求设备推流，或者代理拉网络流，或者从本地文件输入
    //2. 根据目标类型，开启一个网络推流(rtmp/rtsp/srt/ts/domainsock)
}

void U727::stopStream(const string &id) {

}

}