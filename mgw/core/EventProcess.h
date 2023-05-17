#ifndef __MGW_EVENT_PROCESS_H__
#define __MGW_EVENT_PROCESS_H__

#include <memory>

namespace mediakit {

//此类用于监听事件，并根据mgw业务处理这些事件
class EventProcess {
public:
    using Ptr = std::shared_ptr<EventProcess>;
    ~EventProcess();
    static EventProcess::Ptr Instance() {
        //这样的单实例只能使用new构建对象，因为构造函数是私有的
        //make_shared模板没有权限调用私有构造函数
        static Ptr ptr(new EventProcess());
        return ptr;
    }

    void run();
    void exit();

private:
    EventProcess();
    std::string getSnByStreamId(const std::string &streamid);
    std::string getFullUrl(const std::string &schema, const std::string &url, uint16_t port);

};

}

#endif  //__MGW_EVENT_PROCESS_H__