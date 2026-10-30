#include "scope_trigger.h"

#include <math.h>

int scope_trigger_interpolate_at(const uint32_t *words,size_t count,unsigned after_index,
                                 const ScopeTriggerConfig *config,ScopeTriggerResult *result)
{
    if(result!=NULL){result->crossing_sample=0;result->display_start_sample=0;}
    if(words==NULL||config==NULL||result==NULL||count<2||count>4096||
       after_index==0||after_index>=count||config->channel>1||
       (config->rising!=0&&config->rising!=1)||!isfinite(config->level_code)||
       config->level_code<0||config->level_code>1023||config->hysteresis_code<1||config->hysteresis_code>1023||
       !isfinite(config->span_samples)||config->span_samples<=0||
       !isfinite(config->pretrigger_fraction)||config->pretrigger_fraction<0||config->pretrigger_fraction>1)return -1;
    for(size_t i=0;i<count;i++)if(words[i]&UINT32_C(0xf800f800))return -1;
    unsigned shift=config->channel*16;
    unsigned before=(words[after_index-1]>>shift)&0x7ffu,after=(words[after_index]>>shift)&0x7ffu;
    if((before|after)&0x400u)return 0;
    double level=config->level_code;
    if(config->rising?!(before<=level&&after>level):!(before>=level&&after<level))return 0;
    double crossing=after_index-1+(level-before)/((double)after-before);
    double start=crossing-config->pretrigger_fraction*config->span_samples;
    if(!isfinite(crossing)||!isfinite(start)||start<0||start+config->span_samples>count-1)return 0;
    result->crossing_sample=crossing;result->display_start_sample=start;return 1;
}

int scope_trigger_find(const uint32_t *words, size_t count,
                       const ScopeTriggerConfig *config,
                       ScopeTriggerResult *result)
{
    size_t i;
    unsigned shift;
    double level, before = 0.0, candidate = 0.0;
    int previous_valid = 0, armed = 0, pending = 0;

    if (result != NULL) {
        result->crossing_sample = 0.0;
        result->display_start_sample = 0.0;
    }
    if (words == NULL || config == NULL || result == NULL ||
        count < 2 || count > 4096 || config->channel > 1 ||
        (config->rising != 0 && config->rising != 1) ||
        !isfinite(config->level_code) || config->level_code < 0.0 ||
        config->level_code > 1023.0 || config->hysteresis_code < 1 ||
        config->hysteresis_code > 1023 ||
        !isfinite(config->span_samples) || config->span_samples <= 0.0 ||
        !isfinite(config->pretrigger_fraction) ||
        config->pretrigger_fraction < 0.0 || config->pretrigger_fraction > 1.0)
        return -1;

    /* Validate the whole record before accepting an early edge. */
    for (i = 0; i < count; ++i)
        if ((words[i] & UINT32_C(0xf800f800)) != 0)
            return -1;
    if (config->span_samples > (double)(count - 1))
        return 0;

    shift = config->channel * 16;
    /* Reflect falling edges so both directions share the same state machine. */
    level = config->rising ? config->level_code : 1023.0 - config->level_code;
    for (i = 0; i < count; ++i) {
        unsigned sample = (unsigned)(words[i] >> shift) & 0x7ffu;
        double current;
        if ((sample & 0x400u) != 0) {
            previous_valid = armed = pending = 0;
            continue;
        }
        current = config->rising ? (double)sample : 1023.0 - sample;
        if (current <= level - config->hysteresis_code) {
            armed = 1;
            pending = 0;
        }
        if (previous_valid && armed && before <= level && current > level) {
            /* Replace a noise recrossing until the far threshold confirms it. */
            candidate = (double)(i - 1) + (level - before) / (current - before);
            pending = 1;
        }
        if (armed && pending && current >= level + config->hysteresis_code) {
            double start = candidate - config->pretrigger_fraction * config->span_samples;
            if (start >= 0.0 && start + config->span_samples <= (double)(count - 1)) {
                result->crossing_sample = candidate;
                result->display_start_sample = start;
                return 1;
            }
            armed = pending = 0;
        }
        before = current;
        previous_valid = 1;
    }
    return 0;
}
