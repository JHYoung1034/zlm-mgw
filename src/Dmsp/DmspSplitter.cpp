#include "DmspSplitter.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

void DmspSplitter::onParseDmsp(const char *data, size_t size) {

}

ssize_t DmspSplitter::onRecvHeader(const char *data, size_t len) {

    return 0;
}

const char *DmspSplitter::onSearchPacketTail(const char *data, size_t len) {
    
    return nullptr;
}


void DmspSplitter::sendDmsp(const DmspPacket::Ptr &packet) {
    // onSendRawData(obtainBuffer((const void *)&packet->info, sizeof(packet->info)));
    onSendRawData(dynamic_pointer_cast<Buffer>(packet));
}

BufferRaw::Ptr DmspSplitter::obtainBuffer(const void *data, size_t len) {
    auto buffer = _packet_pool.obtain2();
    if (data && len) {
        buffer->assign((const char *) data, len);
    }
    return buffer;
}

}