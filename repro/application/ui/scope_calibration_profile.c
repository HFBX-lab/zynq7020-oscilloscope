#include "scope_calibration_profile.h"
/* Generated from the two raw DC characterization batches. Engineering reference:
 * SDG2122X generator setpoints, without an independent voltage reference.
 * Binding SHA256: bd08dc7a1d258ead0289f3e6436594b4e8a55bf9d70d1187ca248c863f12bbd4 */
const ScopeCalibration *scope_calibration_default_profile(void){
#ifdef SCOPE_ENGINEERING_CALIBRATION
    static const ScopeCalibration profile={
        .flags=UINT32_C(0x103),.id=UINT32_C(0x2b474854),
        .slope_v_per_code={0.0093918182f,0.00912598614f},.offset_v={-4.73163939f,-4.64509344f}};
#else
    static const ScopeCalibration profile={0};
#endif
    return &profile;
}
