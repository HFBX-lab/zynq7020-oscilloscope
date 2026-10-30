#include "scope_calibration.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)

static void close_to(double actual,double expected) {
    CHECK(isfinite(actual));
    CHECK(fabs(actual-expected)<1e-12);
}

static void nominal_conversion(void) {
    ScopeCalibration zero={0};
    CHECK(scope_calibration_valid(NULL));
    CHECK(scope_calibration_valid(&zero));
    for(unsigned ch=0;ch<2;++ch) {
        for(unsigned i=0;i<=1023;++i) {
            double code=i+.25;
            if(code>1023.0)code=1023.0;
            double expected=code*(10.0/1023.0)-5.0;
            close_to(scope_calibration_voltage(NULL,ch,code),expected);
            close_to(scope_calibration_voltage(&zero,ch,code),expected);
            close_to(scope_calibration_code(NULL,ch,expected),code);
        }
        for(int mv=-5000;mv<=5000;++mv) {
            uint32_t expected=(uint32_t)(((uint64_t)(mv+5000)*1023u*256u+5000u)/10000u);
            uint32_t q8=UINT32_MAX;
            CHECK(scope_calibration_trigger_q8(NULL,ch,mv,&q8)==0);
            CHECK(q8==expected);
            CHECK(scope_calibration_trigger_q8(&zero,ch,mv,&q8)==0);
            CHECK(q8==expected);
        }
    }
}

static ScopeCalibration example_profile(void) {
    ScopeCalibration p={SCOPE_CALIBRATION_CHANNEL_MASK|
                        SCOPE_CALIBRATION_GENERATOR_SETPOINTS,UINT32_C(0x12345678),
                        {.0078125f,.01171875f},{-4.0f,-6.0f}};
    return p;
}

static void calibrated_conversion(void) {
    ScopeCalibration p=example_profile();
    ScopeCalibration before=p;
    CHECK(scope_calibration_valid(&p));
    for(unsigned ch=0;ch<2;++ch) {
        for(unsigned i=0;i<=1023;++i) {
            double code=i+.5;
            if(code>1023)code=1023;
            double volts=code*(double)p.slope_v_per_code[ch]+p.offset_v[ch];
            close_to(scope_calibration_voltage(&p,ch,code),volts);
            close_to(scope_calibration_code(&p,ch,volts),code);
        }
        close_to(scope_calibration_voltage(&p,ch,512.0),0.0);
        close_to(scope_calibration_code(&p,ch,12.0),
                 (12.0-p.offset_v[ch])/p.slope_v_per_code[ch]);
        for(int mv=-5000;mv<=5000;++mv) {
            double code=(mv/1000.0-p.offset_v[ch])/p.slope_v_per_code[ch];
            if(code<0)code=0;
            if(code>1023)code=1023;
            uint32_t q8=UINT32_MAX;
            CHECK(scope_calibration_trigger_q8(&p,ch,mv,&q8)==0);
            CHECK(q8==(uint32_t)floor(code*256.0+.5));
        }
    }
    CHECK(!memcmp(&p,&before,sizeof(p)));
    p.id=0; /* A real CRC32 can be zero; mask carries profile identity. */
    CHECK(scope_calibration_valid(&p));
    p.offset_v[0]=-4.0000152587890625f;
    uint32_t q8=0;
    CHECK(scope_calibration_trigger_q8(&p,0,0,&q8)==0);
    CHECK(q8==131073u); /* Exact +half-Q8 rounds upward. */
    p=example_profile();
    CHECK(scope_calibration_trigger_q8(&p,0,-5000,&q8)==0&&q8==0);
    CHECK(scope_calibration_trigger_q8(&p,0,5000,&q8)==0&&q8==1023u*256u);

    /* A non-binary-exact slope must be promoted from the stored float bits. */
    p.slope_v_per_code[0]=.0091f;
    p.offset_v[0]=-4.61f;
    CHECK(scope_calibration_valid(&p));
    double expected=511.75*(double)p.slope_v_per_code[0]+(double)p.offset_v[0];
    CHECK(scope_calibration_voltage(&p,0,511.75)==expected);
}

static void partial_profiles(void) {
    for(unsigned calibrated=0;calibrated<2;++calibrated) {
        unsigned nominal=1-calibrated;
        ScopeCalibration p=example_profile();
        p.flags=SCOPE_CALIBRATION_GENERATOR_SETPOINTS|(1u<<calibrated);
        p.slope_v_per_code[nominal]=0;
        p.offset_v[nominal]=0;
        CHECK(scope_calibration_valid(&p));
        close_to(scope_calibration_voltage(&p,nominal,512.25),
                 512.25*(10.0/1023.0)-5.0);
        for(int mv=-5000;mv<=5000;++mv) {
            uint32_t q8=UINT32_MAX;
            uint32_t expected=(uint32_t)(((uint64_t)(mv+5000)*1023u*256u+5000u)/10000u);
            CHECK(scope_calibration_trigger_q8(&p,nominal,mv,&q8)==0);
            CHECK(q8==expected);
        }
    }
}

static void reject_profile(ScopeCalibration p) {
    uint32_t out=UINT32_C(0xabcdef01);
    ScopeCalibration before=p;
    CHECK(!scope_calibration_valid(&p));
    CHECK(isnan(scope_calibration_voltage(&p,0,512.0)));
    CHECK(isnan(scope_calibration_code(&p,1,0.0)));
    CHECK(scope_calibration_trigger_q8(&p,0,0,&out)==-1);
    CHECK(out==UINT32_C(0xabcdef01));
    CHECK(!memcmp(&p,&before,sizeof(p)));
}

static void profile_validation(void) {
    ScopeCalibration p={0};
    p.id=1;reject_profile(p);
    p=(ScopeCalibration){0};p.slope_v_per_code[0]=.01f;reject_profile(p);
    p=(ScopeCalibration){0};p.offset_v[1]=-5.0f;reject_profile(p);
    p=(ScopeCalibration){0};p.flags=SCOPE_CALIBRATION_GENERATOR_SETPOINTS;reject_profile(p);
    p=example_profile();p.flags=3;reject_profile(p);
    p=example_profile();p.flags|=4;reject_profile(p);
    p=example_profile();p.flags|=UINT32_C(0x80000000);reject_profile(p);
    for(unsigned ch=0;ch<2;++ch) {
        const float bad[]={NAN,INFINITY,-INFINITY,0.0f,-.01f,
                           .0049f,.0151f};
        for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            p=example_profile();p.slope_v_per_code[ch]=bad[i];reject_profile(p);
        }
        const float offsets[]={NAN,INFINITY,-INFINITY,0.0f,1.0f,-20.0f};
        for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
            p=example_profile();p.offset_v[ch]=offsets[i];reject_profile(p);
        }
        p=example_profile();p.flags=SCOPE_CALIBRATION_GENERATOR_SETPOINTS|(1u<<(1-ch));
        p.slope_v_per_code[ch]=0;p.offset_v[ch]=NAN;reject_profile(p);
        p.offset_v[ch]=0;p.slope_v_per_code[ch]=.01f;reject_profile(p);
    }
    p=example_profile();p.slope_v_per_code[0]=.005f;p.offset_v[0]=-2.56f;
    CHECK(scope_calibration_valid(&p));
    p.slope_v_per_code[0]=nextafterf(.005f,0.0f);reject_profile(p);
    p=example_profile();p.slope_v_per_code[0]=.015f;p.offset_v[0]=-7.68f;
    CHECK(scope_calibration_valid(&p));
    p.slope_v_per_code[0]=nextafterf(.015f,INFINITY);reject_profile(p);
    p=example_profile();p.offset_v[0]=-2.0f;CHECK(scope_calibration_valid(&p));
    p.offset_v[0]=nextafterf(-2.0f,0.0f);reject_profile(p);
    p=example_profile();p.offset_v[0]=-6.0f;CHECK(scope_calibration_valid(&p));
    p.offset_v[0]=nextafterf(-6.0f,-INFINITY);reject_profile(p);
}

static void invalid_arguments(void) {
    ScopeCalibration p=example_profile(),before=p;
    const double invalid[]={NAN,INFINITY,-INFINITY};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        CHECK(isnan(scope_calibration_voltage(NULL,0,invalid[i])));
        CHECK(isnan(scope_calibration_voltage(&p,0,invalid[i])));
        CHECK(isnan(scope_calibration_code(NULL,0,invalid[i])));
        CHECK(isnan(scope_calibration_code(&p,1,invalid[i])));
    }
    CHECK(isnan(scope_calibration_voltage(&p,0,-.001)));
    CHECK(isnan(scope_calibration_voltage(&p,1,1023.001)));
    CHECK(isnan(scope_calibration_voltage(NULL,2,512)));
    CHECK(isnan(scope_calibration_code(&p,UINT32_MAX,0)));
    uint32_t out=17;
    CHECK(scope_calibration_trigger_q8(&p,2,0,&out)==-1&&out==17);
    CHECK(scope_calibration_trigger_q8(&p,0,-5001,&out)==-1&&out==17);
    CHECK(scope_calibration_trigger_q8(&p,0,5001,&out)==-1&&out==17);
    CHECK(scope_calibration_trigger_q8(&p,0,0,NULL)==-1);
    CHECK(scope_calibration_trigger_q8(&p,0,0,&p.id)==-1);
    CHECK(!memcmp(&p,&before,sizeof(p)));
    CHECK(scope_calibration_trigger_q8(&p,1,0,&p.flags)==-1);
    CHECK(!memcmp(&p,&before,sizeof(p)));
}

int main(void) {
    nominal_conversion();
    calibrated_conversion();
    partial_profiles();
    profile_validation();
    invalid_arguments();
    printf("CALIBRATION_CORE_TEST_PASS: %u checks\n",checks);
    return 0;
}
