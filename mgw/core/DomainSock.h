#ifndef __MGW_CORE_DOMAIN_SOCK_H__
#define __MGW_CORE_DOMAIN_SOCK_H__

#include "Poller/EventPoller.h"

class DomainSock {
public:
    using Ptr = std::shared_ptr<DomainSock>;


    DomainSock(const toolkit::EventPoller &poller) {}
    ~DomainSock() {}


private:

};

#endif  //__MGW_CORE_DOMAIN_SOCK_H__