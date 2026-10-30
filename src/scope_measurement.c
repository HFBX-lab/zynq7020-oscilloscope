#include "scope_measurement.h"
#include <math.h>
#include <string.h>

static unsigned channel_code(uint32_t word,unsigned channel){return (word>>(channel*16))&1023u;}

typedef struct {
    double sum, minimum, maximum;
    unsigned count;
} TimingRange;

static void timing_range_add(TimingRange *range, double value) {
    if (!range->count || value < range->minimum) range->minimum = value;
    if (!range->count || value > range->maximum) range->maximum = value;
    range->sum += value;
    ++range->count;
}

static bool timing_range_consistent(const TimingRange *range) {
    double mean = range->count ? range->sum / range->count : 0;
    return range->count && range->minimum >= mean * .8 && range->maximum <= mean * 1.2;
}

static bool timing_crosses(double before, double current, double threshold, bool rising) {
    return rising ? before <= threshold && current > threshold
                  : before >= threshold && current < threshold;
}

/* Inspect a single edge inside its surrounding opposite-midpoint half-cycle.
 * This includes threshold ringing on the plateaus, instead of silently choosing
 * the final/first 10/90% crossing. Each half-cycle is scanned a bounded number
 * of times across the complete record, with constant stack space. */
static bool timing_edge(const uint32_t *words, unsigned count, unsigned channel,
                        unsigned middle_index, double low, double mid, double high,
                        bool rising, double *duration) {
    unsigned first = 1, last = count - 1, starts = 0, ends = 0;
    unsigned start_index = 0, end_index = 0;
    double start = 0, end = 0;
    double start_threshold = rising ? low : high, end_threshold = rising ? high : low;
    for (unsigned j = middle_index - 1; j > 0; --j) {
        if (timing_crosses(channel_code(words[j - 1], channel),
                           channel_code(words[j], channel), mid, !rising)) {
            first = j; break;
        }
    }
    for (unsigned j = middle_index + 1; j < count; ++j) {
        if (timing_crosses(channel_code(words[j - 1], channel),
                           channel_code(words[j], channel), mid, !rising)) {
            last = j; break;
        }
    }
    for (unsigned j = first; j <= last; ++j) {
        double before = channel_code(words[j - 1], channel);
        double current = channel_code(words[j], channel);
        if (timing_crosses(before, current, start_threshold, rising)) {
            start = (j - 1) + (start_threshold - before) / (current - before);
            start_index = j; ++starts;
        }
        if (timing_crosses(before, current, end_threshold, rising)) {
            end = (j - 1) + (end_threshold - before) / (current - before);
            end_index = j; ++ends;
        }
    }
    if (starts != 1 || ends != 1 || end - start < 2 || end_index < start_index)
        return false;
    for (unsigned j = start_index; j <= end_index; ++j) {
        unsigned before = channel_code(words[j - 1], channel);
        unsigned current = channel_code(words[j], channel);
        if (rising ? current < before : current > before) return false;
    }
    *duration = end - start;
    return true;
}

int scope_measurement_timing_calibrated(const uint32_t *words, unsigned count,
                            unsigned rate, unsigned channel,
                            const ScopeCalibration *profile,
                            ScopeTimingMeasurement *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!words || !count || count > 4096 || !rate || channel > 1 ||
        !scope_calibration_valid(profile)) return -1;
    unsigned minimum = 1023, maximum = 0;
    bool clipped = false;
    for (unsigned i = 0; i < count; ++i) {
        if (words[i] & UINT32_C(0xf800f800)) return -1;
        unsigned code = channel_code(words[i], channel);
        if (code < minimum) minimum = code;
        if (code > maximum) maximum = code;
        if (!code || code == 1023 || (words[i] & (UINT32_C(1) << (10 + channel * 16))))
            clipped = true;
    }
    double span = maximum - minimum;
    if (clipped || span < 16) return 0;
    double low = minimum + span * .1, mid = minimum + span * .5;
    double high = minimum + span * .9, hysteresis = span * .1;
    out->low_v = scope_calibration_voltage(profile,channel,low);
    out->mid_v = scope_calibration_voltage(profile,channel,mid);
    out->high_v = scope_calibration_voltage(profile,channel,high);
    TimingRange periods = {0}, positives = {0}, negatives = {0};
    TimingRange rises = {0}, falls = {0};
    double candidate = 0, previous_rise = 0, falling = 0;
    unsigned candidate_index = 0, previous_rise_index = 0, falling_index = 0;
    int level = 0, pending = 0;
    bool have_rise = false, have_fall = false, rise_ok = true, fall_ok = true;
    for (unsigned i = 1; i < count; ++i) {
        double before = channel_code(words[i - 1], channel);
        double current = channel_code(words[i], channel);
        if (!level) {
            if (before <= mid - hysteresis) level = -1;
            else if (before >= mid + hysteresis) level = 1;
            else {
                if (current <= mid - hysteresis) level = -1;
                else if (current >= mid + hysteresis) level = 1;
                continue;
            }
        }
        bool rising = level < 0;
        if (!pending && timing_crosses(before, current, mid, rising)) {
            candidate = (i - 1) + (mid - before) / (current - before);
            candidate_index = i; pending = rising ? 1 : -1;
        }
        if (!pending) continue;
        /* Retain the first midpoint crossing through bounded ripple. A full
         * retreat to the starting hysteresis boundary is an unreliable edge;
         * never replace it with a more convenient later crossing. */
        if ((pending > 0 && current <= mid - hysteresis) ||
            (pending < 0 && current >= mid + hysteresis)) return 0;
        if ((pending > 0 && current < mid + hysteresis) ||
            (pending < 0 && current > mid - hysteresis)) continue;
        if (rising) {
            if (have_rise) {
                if (!have_fall) return 0;
                double period = candidate - previous_rise;
                double positive = falling - previous_rise, negative = candidate - falling;
                if (period < 8 || positive < 2 || negative < 2) return 0;
                timing_range_add(&periods, period);
                timing_range_add(&positives, positive);
                timing_range_add(&negatives, negative);
                double edge;
                if (rise_ok) {
                    rise_ok = timing_edge(words, count, channel, previous_rise_index,
                                          low, mid, high, true, &edge);
                    if (rise_ok) timing_range_add(&rises, edge);
                }
                if (fall_ok) {
                    fall_ok = timing_edge(words, count, channel, falling_index,
                                          low, mid, high, false, &edge);
                    if (fall_ok) timing_range_add(&falls, edge);
                }
            }
            previous_rise = candidate; previous_rise_index = candidate_index;
            have_rise = true; have_fall = false;
        } else if (have_rise) {
            if (have_fall) return 0;
            falling = candidate; falling_index = candidate_index; have_fall = true;
        }
        level = -level; pending = 0;
    }
    if (periods.count < 2 || !timing_range_consistent(&periods) ||
        !timing_range_consistent(&positives) || !timing_range_consistent(&negatives)) return 0;
    out->pulse_valid = true; out->cycles = periods.count;
    out->period_s = periods.sum / periods.count / rate;
    out->positive_s = positives.sum / periods.count / rate;
    out->negative_s = negatives.sum / periods.count / rate;
    out->duty_percent = 100 * positives.sum / periods.sum;
    if (rise_ok && rises.count == periods.count && timing_range_consistent(&rises)) {
        out->rise_valid = true; out->rise_s = rises.sum / rises.count / rate;
    }
    if (fall_ok && falls.count == periods.count && timing_range_consistent(&falls)) {
        out->fall_valid = true; out->fall_s = falls.sum / falls.count / rate;
    }
    return 0;
}

bool scope_measurement_frequency(const uint32_t *words,unsigned count,
                                 unsigned rate,unsigned channel,double *frequency_hz){
    if(frequency_hz)*frequency_hz=0;
    if(!words||!frequency_hz||count<2||count>4096||!rate||channel>1)return false;
    unsigned minimum=1023,maximum=0;double sum=0;
    for(unsigned i=0;i<count;i++){
        if(words[i]&UINT32_C(0xf800f800))return false;
        unsigned code=channel_code(words[i],channel);
        if(code<minimum)minimum=code;
        if(code>maximum)maximum=code;
        sum+=code;
    }
    double span=maximum-minimum,mean=sum/count;
    double hysteresis,candidate=0,previous_cross=0;
    double interval_sum=0,interval_min=0,interval_max=0;
    unsigned crossings=0;int armed=0,pending=0;
    if(span<16)return false;
    hysteresis=span*.10;if(hysteresis<4)hysteresis=4;
    for(unsigned i=1;i<count;i++){
        double before=channel_code(words[i-1],channel),current=channel_code(words[i],channel);
        if(before<=mean-hysteresis||current<=mean-hysteresis){armed=1;pending=0;}
        if(armed&&before<=mean&&current>mean){candidate=(i-1)+(mean-before)/(current-before);pending=1;}
        if(armed&&pending&&current>=mean+hysteresis){
            if(crossings){
                double interval=candidate-previous_cross;
                if(crossings==1||interval<interval_min)interval_min=interval;
                if(crossings==1||interval>interval_max)interval_max=interval;
                interval_sum+=interval;
            }
            previous_cross=candidate;++crossings;armed=0;pending=0;
        }
    }
    if(crossings>=3){
        double period=interval_sum/(crossings-1);
        if(interval_min>=4&&interval_min>=period*.8&&interval_max<=period*1.2){
            *frequency_hz=rate/period;return true;
        }
    }
    return false;
}

int scope_measurement_analyze_calibrated(const uint32_t *words, unsigned count,
    unsigned rate,const ScopeCalibration *profile,ScopeMeasurement out[2]) {
    double squared_sum[2] = {0, 0};
    double squared_deviation[2] = {0, 0};
    if (!out) return -1;
    memset(out, 0, 2 * sizeof(*out));
    if (!words || count == 0 || count > 4096 || rate == 0 ||
        !scope_calibration_valid(profile)) return -1;
    for (unsigned i = 0; i < count; ++i)
        if (words[i] & UINT32_C(0xf800f800)) return -1;

    for (unsigned i = 0; i < count; ++i) {
        for (unsigned ch = 0; ch < 2; ++ch) {
            unsigned code = (words[i] >> (16 * ch)) & 1023u;
            double voltage = scope_calibration_voltage(profile,ch,code);
            double delta = voltage - out[ch].vmean_v;
            if (i == 0 || voltage < out[ch].vmin_v) out[ch].vmin_v = voltage;
            if (i == 0 || voltage > out[ch].vmax_v) out[ch].vmax_v = voltage;
            out[ch].vmean_v += delta / (i + 1);
            /* Welford avoids subtracting nearly equal total/DC energies. */
            squared_deviation[ch] += delta * (voltage - out[ch].vmean_v);
            squared_sum[ch] += voltage * voltage;
            if (code == 0 || code == 1023 ||
                (words[i] & (UINT32_C(1) << (10 + 16 * ch))))
                out[ch].overrange = true;
        }
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        out[ch].vpp_v = out[ch].vmax_v - out[ch].vmin_v;
        out[ch].rms_total_v = sqrt(squared_sum[ch] / count);
        out[ch].rms_ac_v = sqrt(squared_deviation[ch] / count);
        out[ch].valid = true;
        (void)scope_measurement_timing_calibrated(words,count,rate,ch,profile,&out[ch].timing);
    }
    return 0;
}

int scope_measurement_timing(const uint32_t *words,unsigned count,
    unsigned rate,unsigned channel,ScopeTimingMeasurement *out){
    return scope_measurement_timing_calibrated(words,count,rate,channel,NULL,out);
}
int scope_measurement_analyze(const uint32_t *words,unsigned count,
    unsigned rate,ScopeMeasurement out[2]){
    return scope_measurement_analyze_calibrated(words,count,rate,NULL,out);
}
