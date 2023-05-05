#ifndef __NETWORK_DMS_CLIENT_H__
#define __NETWORK_DMS_CLIENT_H__

#include <memory>
#include "Socket.h"

namespace toolkit {

class DmsClient : public std::enable_shared_from_this<DmsClient>, public SocketHelper {
public:
    using Ptr = std::shared_ptr<DmsClient>;
    DmsClient(const EventPoller::Ptr &poller = nullptr);
    ~DmsClient() override;

    /**
     * 开始连接tcp服务器
     * @param svr_path 服务器路径
     * @param cli_path 客户端需要绑定的路径
     * @param timeout_sec 超时时间,单位秒
     */
    virtual void startConnect(const std::string &svr_path, const std::string &cli_path = "", float timeout_sec = 5);
    
    /**
     * 主动断开连接
     * @param ex 触发onErr事件时的参数
     */
    void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown")) override;

    /**
     * 连接中或已连接返回true，断开连接时返回false
     */
    virtual bool alive() const;

protected:
    /**
     * 连接服务器结果回调
     * @param ex 成功与否
     */
    virtual void onConnect(const SockException &ex) = 0;

    /**
     * 收到数据回调
     * @param buf 接收到的数据(该buffer会重复使用)
     */
    virtual void onRecv(const Buffer::Ptr &buf) = 0;

    /**
     * 数据全部发送完毕后回调
     */
    virtual void onFlush() {}

    /**
     * 被动断开连接回调
     * @param ex 断开原因
     */
    virtual void onErr(const SockException &ex) = 0;

    /**
     * tcp连接成功后每2秒触发一次该事件
     */
    virtual void onManager() {}

private:
    void onSockConnect(const SockException &ex);

private:
    std::shared_ptr<Timer> _timer;
    //对象个数统计
    ObjectStatistic<DmsClient> _statistic;
};

}
#endif  //__NETWORK_DMS_CLIENT_H__