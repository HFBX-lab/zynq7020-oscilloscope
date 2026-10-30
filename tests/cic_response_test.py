"""Linear CIC transfer analysis; not an ADC/analog bandwidth measurement.

The RTL's integer convolution is tested independently by tb_adc_cic.sv.
This checks passband loss and input aliases folding into DC..0.1*Fs.
It intentionally excludes integer rounding and analog noise.
"""
from pathlib import Path
import json
import math

RATES = [625000, 62500, 6250, 625]


def response(frequency, level):
    result, rate = 1.0, 5e6
    for factor in [8] + [10] * level:
        x = math.pi * frequency / rate
        ratio = abs(math.sin(factor*x)/(factor*math.sin(x))) if abs(math.sin(x)) > 1e-15 else 1.0
        result *= ratio**3
        rate /= factor
    return result


def main():
    profiles = []
    for level, rate in enumerate(RATES):
        minimum = min(response(rate*.1*i/1000, level) for i in range(1001))
        # All image centers below the initial5MSa/s Nyquist boundary, with
        # offsets covering the specified useful output band.
        alias = max(response(k*rate+sign*rate*.1*i/10, level)
                    for k in range(1, int(2.5e6/rate)+1)
                    for sign in [-1, 1] for i in range(11)
                    if 0 < k*rate+sign*rate*.1*i/10 <= 2.5e6)
        loss, rejection = -20*math.log10(minimum), -20*math.log10(max(alias, 1e-300))
        assert loss < .5 and rejection > 45, (level, loss, rejection)
        profiles.append(dict(rate_hz=rate, linear_passband_hz=rate/10,
                             max_passband_loss_db=loss, worst_alias_rejection_db=rejection))
    result = dict(state='offline-only-linear-filter-analysis', analog_bandwidth='not-measured',
                  rounding_noise='not-included', profiles=profiles)
    output = Path(__file__).resolve().parents[1]/'build/long-timebase-filter'
    output.mkdir(parents=True, exist_ok=True)
    (output/'response.json').write_text(json.dumps(result, indent=2)+'\n')
    print('CIC_FREQUENCY_RESPONSE_PASS', json.dumps(profiles))


if __name__ == '__main__':
    main()
