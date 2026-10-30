#ifndef SCOPE_SPECTRUM_H
#define SCOPE_SPECTRUM_H

#include <stdbool.h>
#include <stdint.h>
#include "scope_calibration.h"

#define SCOPE_SPECTRUM_N 4096u
#define SCOPE_SPECTRUM_BINS 2049u

typedef enum {
    SCOPE_SPECTRUM_RECT=0,
    SCOPE_SPECTRUM_HANN=1
} ScopeSpectrumWindow;

typedef struct {
    float real[SCOPE_SPECTRUM_N];
    float imag[SCOPE_SPECTRUM_N];
} ScopeSpectrumWorkspace;

typedef struct {
    bool valid,overrange,peak_valid,remove_mean;
    unsigned count,rate_hz,channel,window;
    double bin_hz,mean_v,coherent_gain;
    double peak_bin,peak_hz,peak_vrms,peak_dbv;
    float vrms[SCOPE_SPECTRUM_BINS];
} ScopeSpectrumResult;

/* Analyze one complete dual-channel ADC record without changing its words.
 * Each channel stores a 10-bit code and OTR flag, with all other bits reserved.
 * Rates are metadata, never instructions to change the ADC clock.
 *
 * Output is a one-sided discrete line-amplitude spectrum in nominal Vrms,
 * normalized by window coherent gain; it is not power spectral density.
 * Hann is periodic: w[n]=.5-.5*cos(2*pi*n/N). Mean removal precedes the window.
 * DC/Nyquist use |X|/sum(w), interior bins use sqrt(2)*|X|/sum(w).
 * Peak frequency may be interpolated; peak Vrms/dBV always describe the
 * strongest actual AC bin, without a parabolic amplitude extrapolation.
 *
 * Success returns 0. Invalid arguments/data return -1 and preserve both out
 * and workspace byte-for-byte. All three buffers must be mutually disjoint.
 * The caller provides valid storage for count words and both full structures.
 * No allocation, recursion or large automatic arrays are used. */
int scope_spectrum_analyze(const uint32_t *words,unsigned count,unsigned rate_hz,
    unsigned channel,ScopeSpectrumWindow window,bool remove_mean,
    ScopeSpectrumWorkspace *workspace,ScopeSpectrumResult *out);
/* Record-owned voltage mapping, including DC and the code-based peak floor.
 * profile may not overlap workspace/out; failures preserve both outputs. */
int scope_spectrum_analyze_calibrated(const uint32_t *words,unsigned count,unsigned rate_hz,
    unsigned channel,ScopeSpectrumWindow window,bool remove_mean,
    const ScopeCalibration *profile,ScopeSpectrumWorkspace *workspace,ScopeSpectrumResult *out);

#endif
