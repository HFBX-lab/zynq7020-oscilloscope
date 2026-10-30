#include "scope_autoset.h"
#include "scope_acquisition.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static ScopeMeasurement signal(double mean, double vpp) {
    ScopeMeasurement m = {0};
    m.valid = true; m.vmin_v = mean-vpp/2; m.vmax_v = mean+vpp/2;
    m.vmean_v = mean; m.vpp_v = vpp; m.rms_ac_v = vpp/sqrt(8.0);
    m.rms_total_v = hypot(mean, m.rms_ac_v);
    return m;
}

static ScopeAutosetResult plan(unsigned conversion, unsigned count, unsigned rate,
    unsigned ch, double f, const ScopeMeasurement m[2], ScopeAutosetPlan *p) {
    memset(p, 0xa5, sizeof(*p));
    return scope_autoset_plan(conversion, count, rate, ch, f, m, p);
}

static void expect_failure(unsigned conversion, unsigned count, unsigned rate,
    unsigned ch, double f, ScopeMeasurement m[2], ScopeAutosetResult expected) {
    ScopeAutosetPlan p, zero = {0};
    CHECK(plan(conversion,count,rate,ch,f,m,&p)==expected);
    CHECK(memcmp(&p,&zero,sizeof(p))==0);
}

static void profiles(void) {
    const unsigned conversions[] = {5000000,10000000,25000000,50000000};
    const unsigned max_rates[] = {625000,10000000,25000000,50000000};
    const unsigned time1[] = {12,17,17,20};
    const unsigned time1k[] = {3,8,8,11};
    const unsigned time50k[] = {0,3,3,6};
    ScopeMeasurement m[2] = {signal(0,2),signal(.5,1)};
    for(unsigned i=0;i<4;++i) {
        const double f[] = {1,1000,50000};
        const unsigned wanted[] = {time1[i],time1k[i],time50k[i]};
        for(unsigned j=0;j<3;++j) {
            ScopeAutosetPlan p;
            unsigned source = j==0?625:j==1?625000:max_rates[i];
            if (i==0 && j==2) {
                expect_failure(conversions[i],4096,source,1,f[j],m,SCOPE_AUTOSET_NO_TIMEBASE);
                continue;
            }
            CHECK(plan(conversions[i],4096,source,1,f[j],m,&p)==SCOPE_AUTOSET_OK);
            CHECK(p.timebase_index==wanted[j]);
            CHECK(p.frequency_hz==f[j]&&p.trigger_channel==1);
            CHECK(p.channel_mask==3&&p.scale_index[0]==2&&p.scale_index[1]==1);
            CHECK(p.position_mdiv[0]==0&&p.position_mdiv[1]==-2500&&p.level_mv==500);
        }
    }
    ScopeAutosetPlan p;
    CHECK(plan(10000000,4096,10000000,0,1e6,m,&p)==SCOPE_AUTOSET_NO_TIMEBASE);
    CHECK(plan(25000000,4096,25000000,0,1e6,m,&p)==SCOPE_AUTOSET_NO_TIMEBASE);
    CHECK(plan(50000000,4096,50000000,0,1e6,m,&p)==SCOPE_AUTOSET_OK&&p.timebase_index==2);
    CHECK(plan(25000000,4096,25000000,0,5e6,m,&p)==SCOPE_AUTOSET_NO_TIMEBASE);
    CHECK(plan(50000000,4096,50000000,0,5e6,m,&p)==SCOPE_AUTOSET_OK&&p.timebase_index==0);
}

static void rejection_and_statistics(void) {
    ScopeMeasurement m[2] = {signal(0,1),signal(.5,1)};
    expect_failure(0,4096,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,0,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,1,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,4097,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,4096,0,1,50000,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,4096,12345,1,50000,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,4096,50000000,2,50000,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,4096,50000000,1,NAN,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,4096,50000000,1,INFINITY,m,SCOPE_AUTOSET_INVALID);
    expect_failure(50000000,4096,50000000,1,0,m,SCOPE_AUTOSET_NO_SIGNAL);
    expect_failure(50000000,4096,50000000,1,-1,m,SCOPE_AUTOSET_NO_SIGNAL);
    expect_failure(50000000,4096,50000000,1,100,m,SCOPE_AUTOSET_NO_SIGNAL);
    expect_failure(50000000,4096,50000000,1,12500001,m,SCOPE_AUTOSET_NO_SIGNAL);
    m[1].valid=false;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_NO_SIGNAL);
    m[1]=signal(.5,1);m[1].overrange=true;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_OVERRANGE);
    m[1]=signal(.5,320.0/1023.0-1e-9);
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_NO_SIGNAL);
    m[1]=signal(.5,320.0/1023.0);
    ScopeAutosetPlan p;
    CHECK(plan(50000000,4096,50000000,1,50000,m,&p)==SCOPE_AUTOSET_OK);
    m[1]=signal(.5,1); m[1].vmean_v=NAN;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    m[1]=signal(.5,1); m[1].vpp_v=3;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    m[1]=signal(.5,1); m[1].vmean_v=2;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    m[1]=signal(.5,1); m[1].rms_total_v=-1;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    m[1]=signal(.5,1); m[1].rms_ac_v=INFINITY;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    m[1]=signal(.5,1); m[1].vmax_v=6; m[1].vpp_v=6;
    expect_failure(50000000,4096,50000000,1,50000,m,SCOPE_AUTOSET_INVALID);
    CHECK(scope_autoset_plan(50000000,4096,50000000,1,50000,m,NULL)==SCOPE_AUTOSET_INVALID);
    CHECK(plan(50000000,4096,50000000,1,50000,NULL,&p)==SCOPE_AUTOSET_INVALID);
    ScopeAutosetPlan zero={0}; CHECK(memcmp(&p,&zero,sizeof p)==0);
}

static void windows_and_vertical(void) {
    ScopeMeasurement m[2]={signal(-3.5,.6),signal(.5,1)};
    ScopeAutosetPlan p;
    CHECK(plan(50000000,4096,50000000,1,50000,m,&p)==SCOPE_AUTOSET_OK);
    CHECK(p.scale_index[0]==3&&p.position_mdiv[0]==3500);
    m[0]=signal(0,0);m[0].valid=false;
    CHECK(plan(50000000,4096,50000000,1,50000,m,&p)==SCOPE_AUTOSET_OK&&p.channel_mask==2);
    m[0]=signal(0,2);m[0].overrange=true;
    CHECK(plan(50000000,4096,50000000,1,50000,m,&p)==SCOPE_AUTOSET_OK&&p.channel_mask==2);
    m[0]=signal(0,2);m[0].vpp_v=NAN;
    CHECK(plan(50000000,4096,50000000,1,50000,m,&p)==SCOPE_AUTOSET_OK&&p.channel_mask==2);
    m[1]=signal(-.500625,1);
    CHECK(plan(50000000,4096,50000000,1,50000,m,&p)==SCOPE_AUTOSET_OK&&p.level_mv==-501);
    CHECK(p.scale_index[1]==1&&p.position_mdiv[1]==2503);
    m[1]=signal(.5,1);
    double lower=2.0*50000000/4095;
    CHECK(plan(50000000,4096,50000000,1,lower,m,&p)==SCOPE_AUTOSET_OK);
    expect_failure(50000000,4096,50000000,1,lower-1e-6,m,SCOPE_AUTOSET_NO_SIGNAL);
    expect_failure(50000000,4096,50000000,1,12500000,m,SCOPE_AUTOSET_NO_TIMEBASE);
    expect_failure(50000000,4096,50000000,1,12500000.01,m,SCOPE_AUTOSET_NO_SIGNAL);
    expect_failure(50000000,10,50000000,1,12500000,m,SCOPE_AUTOSET_NO_TIMEBASE);
    CHECK(plan(50000000,51,50000000,1,5e6,m,&p)==SCOPE_AUTOSET_OK&&p.timebase_index==0);
    expect_failure(50000000,50,50000000,1,5e6,m,SCOPE_AUTOSET_NO_TIMEBASE);
    /* 50 kHz uses 6.25 MHz CIC; 700 kHz uses the high rate RAW profile. */
    CHECK(plan(50000000,4096,50000000,1,50000,m,&p)==SCOPE_AUTOSET_OK&&p.timebase_index==6);
    CHECK(plan(50000000,4096,50000000,1,700000,m,&p)==SCOPE_AUTOSET_OK&&p.timebase_index==2);
    ScopeCapturePlan capture;
    CHECK(scope_acquisition_plan_for(p.timebase_index,50000000,&capture)==0&&capture.filter_kind==SCOPE_FILTER_RAW);
}

/* Independent frequency/profile sweep audits every returned choice against
 * the complete public time label table and every available alternative. */
static void audit_choices(void) {
    const double labels[]={1e-7,2e-7,5e-7,1e-6,2e-6,5e-6,1e-5,2e-5,
        5e-5,1e-4,2e-4,5e-4,.001,.002,.005,.01,.02,.05,.1,.2,.5};
    const unsigned conversion[]={5000000,10000000,25000000,50000000};
    const unsigned offsets[]={8,3,3,0};
    const double frequencies[]={1,10,100,1000,10000,50000,100000,1e6,5e6,1e7};
    const unsigned counts[]={51,257,4096};
    ScopeMeasurement m[2]={signal(-1,2),signal(.5,1)};
    for(unsigned c=0;c<4;++c) {
        for(unsigned rate_index=0;rate_index<21-offsets[c];++rate_index) {
            ScopeCapturePlan source;
            CHECK(scope_acquisition_plan_for(rate_index,conversion[c],&source)==0);
            for(unsigned f=0;f<sizeof(frequencies)/sizeof(frequencies[0]);++f) {
                for(unsigned n=0;n<sizeof(counts)/sizeof(counts[0]);++n) {
                    ScopeAutosetPlan p, zero={0};
                    ScopeAutosetResult r=plan(conversion[c],counts[n],source.rate_hz,1,frequencies[f],m,&p);
                    bool source_ok=frequencies[f]>=2.0*source.rate_hz/(counts[n]-1)&&
                        frequencies[f]<=source.rate_hz/4.0;
                    unsigned available=0;
                    double selected_score=0;
                    if(r==SCOPE_AUTOSET_OK) {
                        CHECK(source_ok&&p.timebase_index<21-offsets[c]);
                        selected_score=fabs(log(frequencies[f]*10*labels[p.timebase_index+offsets[c]]/4));
                    } else CHECK(memcmp(&p,&zero,sizeof(p))==0);
                    for(unsigned i=0;i<21-offsets[c];++i) {
                        ScopeCapturePlan target;
                        CHECK(scope_acquisition_plan_for(i,conversion[c],&target)==0);
                        double window=10*labels[i+offsets[c]];
                        double periods=window*frequencies[f];
                        bool eligible=window*target.rate_hz<=counts[n]-1&&periods>=2&&periods<=8&&
                            frequencies[f]<=target.rate_hz/(target.filter_kind==SCOPE_FILTER_RAW?4.0:10.0);
                        if(eligible) {
                            ++available;
                            if(r==SCOPE_AUTOSET_OK) CHECK(selected_score<=fabs(log(periods/4))+1e-12);
                        }
                        if(r==SCOPE_AUTOSET_OK&&i==p.timebase_index) CHECK(eligible);
                    }
                    CHECK(r==(!source_ok?SCOPE_AUTOSET_NO_SIGNAL:available?SCOPE_AUTOSET_OK:SCOPE_AUTOSET_NO_TIMEBASE));
                }
            }
        }
    }
}

static void asymmetric_pulse_fits(void) {
    ScopeMeasurement m[2]={{0}};ScopeAutosetPlan plan;
    /* Independent 10%-duty 0..1V pulse statistics: mean=.1, E[v²]=.1.
       Vpp alone fits .2V/div, but mean-centering would put its peak at4.5div. */
    m[0]=(ScopeMeasurement){.valid=true,.vmin_v=0,.vmax_v=1,.vmean_v=.1,
        .vpp_v=1,.rms_total_v=sqrt(.1),.rms_ac_v=.3};
    CHECK(scope_autoset_plan(50000000,4096,50000000,0,50000,m,&plan)==SCOPE_AUTOSET_OK);
    static const double scale[]={.1,.2,.5,1,2,5,10};
    CHECK(plan.scale_index[0]==2);
    CHECK(m[0].vmax_v/scale[plan.scale_index[0]]+plan.position_mdiv[0]/1000.0<=3.5);
    CHECK(m[0].vmin_v/scale[plan.scale_index[0]]+plan.position_mdiv[0]/1000.0>=-3.5);
}

int main(void) {
    profiles(); rejection_and_statistics(); windows_and_vertical(); audit_choices(); asymmetric_pulse_fits();
    printf("scope_autoset: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
