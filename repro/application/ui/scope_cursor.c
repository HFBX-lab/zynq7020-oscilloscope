#include "scope_cursor.h"
#include <math.h>
#include <string.h>
int scope_cursor_measure_calibrated(const uint32_t *words,size_t count,unsigned rate,
                         double start,double span,const unsigned positions[2],
                         const ScopeCalibration *profile,
                         ScopeCursorResult *result){
    if(result)memset(result,0,sizeof(*result));
    if(!words||count<2||count>4096||!rate||!positions||!result||
       !isfinite(start)||start<0||!isfinite(span)||span<=0||!isfinite(start+span)||
       positions[0]>1000||positions[1]>1000||!scope_calibration_valid(profile))return -1;
    for(size_t i=0;i<count;i++)if(words[i]&UINT32_C(0xf800f800))return -1;
    for(unsigned p=0;p<2;p++){
        double offset=span*(positions[p]/1000.0),at=start+offset;
        result->time_s[p]=offset/rate;
        if(at>(double)(count-1))continue;
        size_t n=(size_t)at;double fraction=at-n;
        for(unsigned ch=0;ch<2;ch++){
            unsigned a=(words[n]>>(16*ch))&2047u,b=a;
            if(fraction>0)b=(words[n+1]>>(16*ch))&2047u;
            if((a|b)&1024u)continue;
            result->voltage_v[ch][p]=scope_calibration_voltage(profile,ch,a+fraction*((double)b-a));
            result->point_valid[ch][p]=true;
        }
    }
    result->delta_s=result->time_s[1]-result->time_s[0];
    if(result->delta_s!=0){
        result->reciprocal_hz=1/fabs(result->delta_s);
        result->reciprocal_valid=isfinite(result->reciprocal_hz);
    }
    for(unsigned ch=0;ch<2;ch++)if(result->point_valid[ch][0]&&result->point_valid[ch][1]){
        result->delta_valid[ch]=true;
        result->delta_v[ch]=result->voltage_v[ch][1]-result->voltage_v[ch][0];
    }
    return 0;
}
int scope_cursor_measure(const uint32_t *words,size_t count,unsigned rate,
    double start,double span,const unsigned positions[2],ScopeCursorResult *result){
    return scope_cursor_measure_calibrated(words,count,rate,start,span,positions,NULL,result);
}
