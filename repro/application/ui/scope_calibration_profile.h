#ifndef SCOPE_CALIBRATION_PROFILE_H
#define SCOPE_CALIBRATION_PROFILE_H
#include "scope_calibration.h"
/* Default is nominal. Only an explicit engineering build enables the profile. */
const ScopeCalibration *scope_calibration_default_profile(void);
#endif
