#ifndef __HVS_SRT_PLAYER_H__
#define __HVS_SRT_PLAYER_H__

#include "Network/Socket.h"
#include "Player/PlayerBase.h"

namespace MGW {

class SrtPlayer : public mediakit::PlayerBase, public toolkit::SocketHelper {
public:
    using Ptr = std::shared_ptr<SrtPlayer>;
    SrtPlayer();
    ~SrtPlayer();

    //PlayerBase interface
    void play(const std::string &url) override;

};

}
#endif  //__HVS_SRT_PLAYER_H__