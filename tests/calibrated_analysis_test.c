#include "scope_calibration.h"
#include "scope_measurement.h"
#include "scope_cursor.h"
#include "scope_spectrum.h"
#include "scope_autoset.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Declarations precede integration so the first link verifies the missing APIs. */
int scope_measurement_analyze_calibrated(const uint32_t *,unsigned,unsigned,
    const ScopeCalibration *,ScopeMeasurement [2]);
int scope_measurement_timing_calibrated(const uint32_t *,unsigned,unsigned,unsigned,
    const ScopeCalibration *,ScopeTimingMeasurement *);
int scope_cursor_measure_calibrated(const uint32_t *,size_t,unsigned,double,double,
    const unsigned [2],const ScopeCalibration *,ScopeCursorResult *);
int scope_spectrum_analyze_calibrated(const uint32_t *,unsigned,unsigned,unsigned,
    ScopeSpectrumWindow,bool,const ScopeCalibration *,ScopeSpectrumWorkspace *,ScopeSpectrumResult *);
ScopeAutosetResult scope_autoset_plan_calibrated(unsigned,unsigned,unsigned,unsigned,
    double,const ScopeMeasurement [2],const ScopeCalibration *,ScopeAutosetPlan *);

static uint32_t words[4096];
static ScopeSpectrumWorkspace workspace;
static ScopeSpectrumResult nominal_fft,calibrated_fft,saved_fft;
static unsigned checks;
static void near(double a,double b,double tolerance){assert(fabs(a-b)<=tolerance);++checks;}

int main(void){
    const ScopeCalibration profile={.flags=0x103,.id=123,
        .slope_v_per_code={.009f,.011f},.offset_v={-4.55f,-5.3f}};
    const unsigned positions[2]={0,1000};
    assert(scope_calibration_valid(&profile));
    for(unsigned i=0;i<4096;i++){
        unsigned a=(unsigned)lround(500+50*sin(2*3.14159265358979323846*i/64));
        unsigned b=(unsigned)lround(480+40*cos(2*3.14159265358979323846*i/64));
        words[i]=a|(b<<16);
    }
    ScopeMeasurement nominal[2],calibrated[2],same[2];
    assert(!scope_measurement_analyze(words,4096,64000,nominal));
    assert(!scope_measurement_analyze_calibrated(words,4096,64000,&profile,calibrated));
    assert(!scope_measurement_analyze_calibrated(words,4096,64000,NULL,same));
    assert(!memcmp(nominal,same,sizeof(same)));
    ScopeCursorResult old_cursor,new_cursor;
    assert(!scope_cursor_measure(words,4096,64000,11.25,900.5,positions,&old_cursor));
    assert(!scope_cursor_measure_calibrated(words,4096,64000,11.25,900.5,positions,&profile,&new_cursor));
    for(unsigned ch=0;ch<2;ch++){
        double gain=(double)profile.slope_v_per_code[ch]/(10.0/1023.0);
        double offset=profile.offset_v[ch]+5*gain;
        near(calibrated[ch].vmin_v,gain*nominal[ch].vmin_v+offset,1e-12);
        near(calibrated[ch].vmax_v,gain*nominal[ch].vmax_v+offset,1e-12);
        near(calibrated[ch].vmean_v,gain*nominal[ch].vmean_v+offset,1e-12);
        near(calibrated[ch].vpp_v,gain*nominal[ch].vpp_v,1e-12);
        near(calibrated[ch].rms_ac_v,gain*nominal[ch].rms_ac_v,1e-12);
        near(calibrated[ch].rms_total_v,hypot(calibrated[ch].rms_ac_v,calibrated[ch].vmean_v),1e-12);
        const ScopeTimingMeasurement *a=&nominal[ch].timing,*b=&calibrated[ch].timing;
        assert(a->pulse_valid&&b->pulse_valid);
        near(a->positive_s,b->positive_s,0);near(a->negative_s,b->negative_s,0);
        near(a->duty_percent,b->duty_percent,0);
        near(b->low_v,gain*a->low_v+offset,1e-12);
        near(b->mid_v,gain*a->mid_v+offset,1e-12);
        near(b->high_v,gain*a->high_v+offset,1e-12);
        ScopeTimingMeasurement direct;
        assert(!scope_measurement_timing_calibrated(words,4096,64000,ch,&profile,&direct));
        assert(!memcmp(&direct,b,sizeof(direct)));
        for(unsigned p=0;p<2;p++){
            assert(new_cursor.point_valid[ch][p]);
            near(new_cursor.voltage_v[ch][p],gain*old_cursor.voltage_v[ch][p]+offset,1e-12);
            near(new_cursor.time_s[p],old_cursor.time_s[p],0);
        }
        near(new_cursor.delta_v[ch],gain*old_cursor.delta_v[ch],1e-12);
        for(unsigned window=0;window<2;window++){
            assert(!scope_spectrum_analyze(words,4096,64000,ch,(ScopeSpectrumWindow)window,true,&workspace,&nominal_fft));
            assert(!scope_spectrum_analyze_calibrated(words,4096,64000,ch,(ScopeSpectrumWindow)window,true,&profile,&workspace,&calibrated_fft));
            near(calibrated_fft.mean_v,gain*nominal_fft.mean_v+offset,1e-12);
            for(unsigned bin=0;bin<2049;bin++)near(calibrated_fft.vrms[bin],gain*nominal_fft.vrms[bin],2e-7);
            assert(calibrated_fft.peak_valid==nominal_fft.peak_valid);
            near(calibrated_fft.peak_hz,nominal_fft.peak_hz,.001);
        }
        assert(!scope_spectrum_analyze_calibrated(words,4096,64000,ch,SCOPE_SPECTRUM_RECT,false,&profile,&workspace,&calibrated_fft));
        near(calibrated_fft.vrms[0],fabs(calibrated[ch].vmean_v),1e-7);
    }
    ScopeCalibration invalid=profile;invalid.slope_v_per_code[0]=NAN;
    memset(calibrated,0x55,sizeof(calibrated));
    assert(scope_measurement_analyze_calibrated(words,4096,64000,&invalid,calibrated)==-1);
    memset(same,0,sizeof(same));assert(!memcmp(same,calibrated,sizeof(same)));
    saved_fft=calibrated_fft;
    assert(scope_spectrum_analyze_calibrated(words,4096,64000,0,SCOPE_SPECTRUM_RECT,false,&invalid,&workspace,&calibrated_fft)==-1);
    assert(!memcmp(&saved_fft,&calibrated_fft,sizeof(saved_fft)));
    words[12]|=1u<<26;
    assert(!scope_measurement_analyze_calibrated(words,4096,64000,&profile,calibrated));
    assert(!calibrated[0].overrange&&calibrated[1].overrange);
    ScopeMeasurement small[2]={{0}};
    small[0].valid=true;small[0].vmin_v=-.2;small[0].vmax_v=.1;
    small[0].vmean_v=-.05;small[0].vpp_v=.3;
    small[0].rms_ac_v=.3/sqrt(8);small[0].rms_total_v=hypot(.05,small[0].rms_ac_v);
    ScopeAutosetPlan plan;
    assert(scope_autoset_plan(50000000,4096,6250000,0,50000,small,&plan)==SCOPE_AUTOSET_NO_SIGNAL);
    assert(scope_autoset_plan_calibrated(50000000,4096,6250000,0,50000,small,&profile,&plan)==SCOPE_AUTOSET_OK);
    small[0].vmin_v=-.19;small[0].vmax_v=.09;small[0].vpp_v=.28;
    small[0].rms_ac_v=.28/sqrt(8);small[0].rms_total_v=hypot(.05,small[0].rms_ac_v);
    assert(scope_autoset_plan_calibrated(50000000,4096,6250000,0,50000,small,&profile,&plan)==SCOPE_AUTOSET_NO_SIGNAL);
    assert(scope_autoset_plan_calibrated(50000000,4096,6250000,0,50000,small,&invalid,&plan)==SCOPE_AUTOSET_INVALID);
    /* A legal voltage mapping may put a signal midpoint outside the UI's
     * supported trigger range. Reject rather than enter a hardware timeout. */
    ScopeCalibration far_profile={.flags=0x101,.slope_v_per_code={.015f,0},.offset_v={-11,0}};
    assert(scope_calibration_valid(&far_profile));
    small[0].vmean_v=-8;small[0].vmin_v=-8.75;small[0].vmax_v=-7.25;small[0].vpp_v=1.5;
    small[0].rms_ac_v=1.5/sqrt(8);small[0].rms_total_v=hypot(8,small[0].rms_ac_v);
    memset(&plan,0x55,sizeof(plan));
    assert(scope_autoset_plan_calibrated(50000000,4096,6250000,0,50000,small,&far_profile,&plan)==SCOPE_AUTOSET_INVALID);
    ScopeAutosetPlan zero_plan={0};assert(!memcmp(&plan,&zero_plan,sizeof(plan)));
    printf("CALIBRATED_ANALYSIS_PASS %u checks\n",checks);
    return 0;
}
