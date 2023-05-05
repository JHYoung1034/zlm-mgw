#ifndef __MGW_TRAFFICS_STATISTICS_H__
#define __MGW_TRAFFICS_STATISTICS_H__

#include <memory>

//此类用于流量统计，推流中断重推不间断的统计
class TrafficsStatistics {
public:
    using Ptr = std::shared_ptr<TrafficsStatistics>;
    
};

#endif  //__MGW_TRAFFICS_STATISTICS_H__