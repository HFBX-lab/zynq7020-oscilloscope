#include "scope_autoset.h"
#include "scope_acquisition.h"
#include <math.h>
#include <string.h>

static const double scales[] = {.1,.2,.5,1,2,5,10};
static const double legacy_timebase[] = {
    .00005,.0001,.0002,.0005,.001,.002,.005,.01,.02,.05,.1,.2,.5
};
static const double fast_timebase[] = {
    .0000001,.0000002,.0000005,.000001,.000002,.000005,.00001,.00002
};

static bool consistent(const ScopeMeasurement *m,const ScopeCalibration *profile,unsigned channel) {
    const double epsilon = 1e-9;
    if (!isfinite(m->vmin_v) || !isfinite(m->vmax_v) ||
        !isfinite(m->vmean_v) || !isfinite(m->vpp_v) ||
        !isfinite(m->rms_total_v) || !isfinite(m->rms_ac_v)) return false;
    if (m->vmin_v < scope_calibration_voltage(profile,channel,0) ||
        m->vmax_v > scope_calibration_voltage(profile,channel,1023) ||
        m->vmin_v > m->vmean_v || m->vmean_v > m->vmax_v ||
        m->vpp_v < 0 || m->rms_total_v < 0 || m->rms_ac_v < 0) return false;
    if (fabs(m->vpp_v-(m->vmax_v-m->vmin_v)) > epsilon ||
        m->rms_total_v+epsilon < fabs(m->vmean_v) ||
        m->rms_total_v > fmax(fabs(m->vmin_v),fabs(m->vmax_v))+epsilon ||
        m->rms_ac_v > m->vpp_v/2+epsilon) return false;
    return fabs(m->rms_total_v*m->rms_total_v-
        m->rms_ac_v*m->rms_ac_v-m->vmean_v*m->vmean_v) <= epsilon;
}

ScopeAutosetResult scope_autoset_plan_calibrated(unsigned conversion_hz, unsigned count,
    unsigned source_rate, unsigned channel, double frequency_hz,
    const ScopeMeasurement measurements[2],const ScopeCalibration *profile,ScopeAutosetPlan *out) {
    ScopeAutosetPlan result = {0};
    unsigned prefix, total;
    bool source_supported = false, found = false;
    double best = INFINITY;
    if (!out) return SCOPE_AUTOSET_INVALID;
    memset(out, 0, sizeof(*out));
    if (!measurements || channel > 1 || count < 2 || count > 4096 ||
        !source_rate || !isfinite(frequency_hz) || !scope_calibration_valid(profile)) return SCOPE_AUTOSET_INVALID;
    switch (conversion_hz) {
        case 5000000: prefix=0; break;
        case 10000000: case 25000000: prefix=5; break;
        case 50000000: prefix=8; break;
        default: return SCOPE_AUTOSET_INVALID;
    }
    total = prefix+SCOPE_TIMEBASE_COUNT;
    for (unsigned i=0;i<total;++i) {
        ScopeCapturePlan capture;
        if (!scope_acquisition_plan_for(i,conversion_hz,&capture) &&
            capture.rate_hz==source_rate) source_supported=true;
    }
    if (!source_supported) return SCOPE_AUTOSET_INVALID;
    if (!measurements[channel].valid) return SCOPE_AUTOSET_NO_SIGNAL;
    if (measurements[channel].overrange) return SCOPE_AUTOSET_OVERRANGE;
    if (!consistent(&measurements[channel],profile,channel)) return SCOPE_AUTOSET_INVALID;
    double code_step=(scope_calibration_voltage(profile,channel,1023)-scope_calibration_voltage(profile,channel,0))/1023;
    if (measurements[channel].vpp_v < 32*code_step ||
        frequency_hz < 2.0*source_rate/(count-1) ||
        frequency_hz > source_rate/4.0) return SCOPE_AUTOSET_NO_SIGNAL;

    for (unsigned i=0;i<total;++i) {
        ScopeCapturePlan capture;
        double seconds = i<prefix?fast_timebase[i+8-prefix]:legacy_timebase[i-prefix];
        double cycles = frequency_hz*seconds*10;
        double score;
        if (scope_acquisition_plan_for(i,conversion_hz,&capture)) continue;
        if (seconds*10*capture.rate_hz > count-1 ||
            frequency_hz > capture.rate_hz/(capture.filter_kind==SCOPE_FILTER_RAW?4.0:10.0) ||
            cycles < 2 || cycles > 8) continue;
        score = fabs(log(cycles/4));
        if (score < best) {
            best=score; result.timebase_index=i; found=true;
        }
    }
    if (!found) return SCOPE_AUTOSET_NO_TIMEBASE;
    for (unsigned ch=0;ch<2;++ch) {
        const ScopeMeasurement *m=&measurements[ch];
        if (!m->valid || m->overrange || !consistent(m,profile,ch)) continue;
        for (unsigned scale=0;scale<sizeof(scales)/sizeof(scales[0]);++scale) {
            if (m->vpp_v <= 6*scales[scale] && fabs(m->vmean_v) <= 3.5*scales[scale]) {
                int position=(int)lround(-m->vmean_v/scales[scale]*1000);
                /* A pulse can be highly asymmetric about its mean. Check
                   both extrema after the actual millidiv rounding as well
                   as the total span, so a stable narrow pulse stays visible. */
                if(m->vmin_v/scales[scale]+position/1000.0 < -3.5 ||
                   m->vmax_v/scales[scale]+position/1000.0 > 3.5) continue;
                result.scale_index[ch]=scale;
                result.position_mdiv[ch]=position;
                result.channel_mask |= 1u<<ch;
                break;
            }
        }
    }
    result.trigger_channel=channel;
    result.level_mv=(int)lround((measurements[channel].vmin_v+
        measurements[channel].vmax_v)*500);
    if(result.level_mv < -5000||result.level_mv>5000)return SCOPE_AUTOSET_INVALID;
    result.frequency_hz=frequency_hz;
    *out=result;
    return SCOPE_AUTOSET_OK;
}

ScopeAutosetResult scope_autoset_plan(unsigned conversion_hz,unsigned count,
    unsigned source_rate,unsigned channel,double frequency_hz,
    const ScopeMeasurement measurements[2],ScopeAutosetPlan *out){
    return scope_autoset_plan_calibrated(conversion_hz,count,source_rate,channel,
        frequency_hz,measurements,NULL,out);
}
