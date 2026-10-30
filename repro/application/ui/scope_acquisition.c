#include "scope_acquisition.h"
#include <stddef.h>

int scope_acquisition_plan(unsigned timebase_index,ScopeCapturePlan *out){
    return scope_acquisition_plan_for(timebase_index,5000000,out);
}
int scope_acquisition_plan_for(unsigned timebase_index,unsigned conversion_hz,ScopeCapturePlan *out){
    static const unsigned rates[]={625000,62500,6250,625};
    unsigned legacy_index=timebase_index,index;
    if(out==NULL||(conversion_hz!=5000000&&conversion_hz!=10000000&&conversion_hz!=25000000&&conversion_hz!=50000000)||
       timebase_index>=(conversion_hz==50000000?SCOPE_50M_TIMEBASE_COUNT:
                       conversion_hz!=5000000?SCOPE_HIGHSPEED_TIMEBASE_COUNT:SCOPE_TIMEBASE_COUNT))return -1;
    if(conversion_hz==50000000){
        index=timebase_index<6?4:timebase_index<9?5:timebase_index<12?0:
            timebase_index<15?1:timebase_index<18?2:3;
    }else if(conversion_hz==25000000){
        index=timebase_index<4?4:timebase_index<6?5:timebase_index<9?0:
            timebase_index<12?1:timebase_index<15?2:3;
    }else if(conversion_hz==10000000&&timebase_index<5)index=4;
    else{
        if(conversion_hz==10000000)legacy_index-=5;
        index=legacy_index<4?0:legacy_index<7?1:legacy_index<10?2:3;
    }
    out->rate_index=index;out->rate_hz=index==4?conversion_hz:index==5?6250000:rates[index];out->count=4096;out->conversion_hz=conversion_hz;
    out->filter_kind=index==4?SCOPE_FILTER_RAW:SCOPE_FILTER_CIC3;
    out->timeout_ms=(out->count*1000u+out->rate_hz-1u)/out->rate_hz+100u;
    return 0;
}
