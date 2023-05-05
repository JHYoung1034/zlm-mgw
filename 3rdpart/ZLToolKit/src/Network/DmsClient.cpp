#include "DmsClient.h"

using namespace std;

namespace toolkit {

StatisticImp(DmsClient)

DmsClient::DmsClient(const EventPoller::Ptr &poller) : SocketHelper(nullptr) {
    setPoller(poller ? poller : EventPollerPool::Instance().getPoller());
    setOnCreateSocket([](const EventPoller::Ptr &poller) {
        return Socket::createSocket(poller, true);
    });
}

DmsClient::~DmsClient() {}

void DmsClient::shutdown(const SockException &ex) {
    _timer.reset();
    SocketHelper::shutdown(ex);
}

bool DmsClient::alive() const {
    if (_timer) {
        //连接中或已连接
        return true;
    }
    //在websocket client(zlmediakit)相关代码中，
    //_timer一直为空，但是socket fd有效，alive状态也应该返回true
    auto sock = getSock();
    return sock && sock->rawFD() >= 0;
}

void DmsClient::startConnect(const std::string &svr_path, const std::string &cli_path, float timeout_sec) {
    weak_ptr<DmsClient> weak_self = shared_from_this();

    //2秒钟回调一次onManager
    _timer = std::make_shared<Timer>(2.0f, [weak_self]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->onManager();
        return true;
    }, getPoller());

    auto sock_ptr = getSock().get();
    sock_ptr->setOnErr([weak_self, sock_ptr](const SockException &ex) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //已经重连socket，上次的socket的事件忽略掉
            return;
        }
        strong_self->_timer.reset();
        strong_self->onErr(ex);
    });

    sock_ptr->connect(svr_path, [weak_self](const SockException &err) {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->onSockConnect(err);
        }
    }, cli_path, timeout_sec);
}

void DmsClient::onSockConnect(const SockException &ex) {
    if (ex) {
        //连接失败
        _timer.reset();
        onConnect(ex);
        return;
    }

    auto sock_ptr = getSock().get();
    weak_ptr<DmsClient> weak_self = shared_from_this();

    sock_ptr->setOnFlush([weak_self, sock_ptr]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //已经重连socket，上传socket的事件忽略掉
            return false;
        }
        strong_self->onFlush();
        return true;
    });

    sock_ptr->setOnRead([weak_self, sock_ptr](const Buffer::Ptr &pBuf, struct sockaddr *, int) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //已经重连socket，上传socket的事件忽略掉
            return;
        }
        try {
            strong_self->onRecv(pBuf);
        } catch (std::exception &ex) {
            strong_self->shutdown(SockException(Err_other, ex.what()));
        }
    });

    onConnect(ex);
}

}