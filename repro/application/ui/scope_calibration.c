#include "scope_calibration.h"

#include <math.h>
#include <stddef.h>

bool scope_calibration_valid(const ScopeCalibration *profile) {
    if(!profile)return true;
    uint32_t mask=profile->flags&SCOPE_CALIBRATION_CHANNEL_MASK;
    if(profile->flags&~(SCOPE_CALIBRATION_CHANNEL_MASK|
                       SCOPE_CALIBRATION_GENERATOR_SETPOINTS))return false;
    if(mask) {
        if(!(profile->flags&SCOPE_CALIBRATION_GENERATOR_SETPOINTS))return false;
    }else if(profile->flags||profile->id)return false;
    for(unsigned ch=0;ch<2;++ch) {
        double slope=profile->slope_v_per_code[ch],offset=profile->offset_v[ch];
        if(mask&(1u<<ch)) {
            if(!isfinite(slope)||!isfinite(offset)||
               slope<.005f||slope>.015f)return false;
            double zero_code=-offset/slope;
            if(zero_code<256.0||zero_code>768.0)return false;
        }else if(slope!=0||offset!=0)return false;
    }
    return true;
}

static int calibrated(const ScopeCalibration *profile,unsigned channel) {
    return profile&&(profile->flags&(1u<<channel));
}

double scope_calibration_voltage(const ScopeCalibration *profile,
                                 unsigned channel,double code) {
    if(!scope_calibration_valid(profile)||channel>1||!isfinite(code)||
       code<0||code>1023)return NAN;
    if(calibrated(profile,channel))
        return code*(double)profile->slope_v_per_code[channel]+profile->offset_v[channel];
    return code*(10.0/1023.0)-5.0;
}

double scope_calibration_code(const ScopeCalibration *profile,
                              unsigned channel,double volts) {
    if(!scope_calibration_valid(profile)||channel>1||!isfinite(volts))return NAN;
    if(calibrated(profile,channel))
        return (volts-(double)profile->offset_v[channel])/profile->slope_v_per_code[channel];
    return (volts+5.0)/(10.0/1023.0);
}

static int output_overlaps(const ScopeCalibration *profile,const uint32_t *out) {
    if(!profile)return 0;
    uintptr_t a=(uintptr_t)profile,b=(uintptr_t)out;
    return a<=b?b-a<sizeof(*profile):a-b<sizeof(*out);
}

int scope_calibration_trigger_q8(const ScopeCalibration *profile,
                                 unsigned channel,int level_mv,uint32_t *out) {
    if(!out||!scope_calibration_valid(profile)||channel>1||
       level_mv < -5000||level_mv>5000||output_overlaps(profile,out))return -1;
    if(calibrated(profile,channel)) {
        double code=scope_calibration_code(profile,channel,level_mv/1000.0);
        if(code<0)code=0;
        if(code>1023)code=1023;
        *out=(uint32_t)lround(code*256.0);
    }else {
        uint64_t numerator=(uint64_t)(level_mv+5000)*1023u*256u;
        *out=(uint32_t)((numerator+5000u)/10000u);
    }
    return 0;
}
