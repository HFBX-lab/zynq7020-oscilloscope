"""Independent event-list reference for record timing; never calls production C."""
import math


def timing_reference(words, rate, channel):
    assert 0 < len(words) <= 4096 and rate > 0 and channel in (0, 1)
    assert all(not (w & 0xf800f800) for w in words)
    values = [(w >> (channel * 16)) & 1023 for w in words]
    result = dict(pulse_valid=False, rise_valid=False, fall_valid=False, cycles=0,
                  duty_percent=0., positive_us=0., negative_us=0., period_us=0.,
                  rise_us=0., fall_us=0., low_v=0., mid_v=0., high_v=0.)
    bottom, top = min(values), max(values)
    if top - bottom < 16 or bottom == 0 or top == 1023 or any(
            w & (1 << (10 + 16 * channel)) for w in words):
        return result
    span = top - bottom
    levels = [bottom + span * p for p in (.1, .4, .5, .6, .9)]
    low, lower, mid, upper, high = levels
    result.update(low_v=low * 10 / 1023 - 5, mid_v=mid * 10 / 1023 - 5,
                  high_v=high * 10 / 1023 - 5)

    def crossings(threshold, rising):
        indices = [i for i in range(1, len(values)) if
                   (values[i - 1] <= threshold < values[i] if rising else
                    values[i - 1] >= threshold > values[i])]
        return [(i, i - 1 + (threshold - values[i - 1]) /
                 (values[i] - values[i - 1])) for i in indices]

    all_crossings = sorted([(i, t, True) for i, t in crossings(mid, True)] +
                           [(i, t, False) for i, t in crossings(mid, False)])
    stable = [(i, -1 if v <= lower else 1) for i, v in enumerate(values)
              if v <= lower or v >= upper]
    if not stable:
        return result
    start, polarity = stable[0]
    confirmed = []
    blocked_until = start
    for index, time, rising in all_crossings:
        if index <= blocked_until or rising != (polarity < 0):
            continue
        confirmations = [(i, v >= upper if rising else v <= lower)
                         for i, v in enumerate(values[index:], index)
                         if v <= lower or v >= upper]
        if not confirmations:
            break
        confirm, reached = confirmations[0]
        if not reached:
            return result
        confirmed.append((index, time, rising))
        blocked_until, polarity = confirm, -polarity
    rises = [item for item in confirmed if item[2]]
    cycles = []
    for first, following in zip(rises, rises[1:]):
        falls = [item for item in confirmed if not item[2] and
                 first[0] < item[0] < following[0]]
        if len(falls) != 1:
            return result
        fall = falls[0]
        period = following[1] - first[1]
        positive, negative = fall[1] - first[1], following[1] - fall[1]
        if period < 8 or min(positive, negative) < 2:
            return result
        cycles.append((first, fall, period, positive, negative))

    def consistent(numbers):
        if not numbers:
            return False
        mean = math.fsum(numbers) / len(numbers)
        return min(numbers) >= .8 * mean and max(numbers) <= 1.2 * mean

    if len(cycles) < 2 or not all(consistent([c[i] for c in cycles])
                                  for i in (2, 3, 4)):
        return result
    periods = math.fsum(c[2] for c in cycles)
    positives = math.fsum(c[3] for c in cycles)
    factor = 1e6 / rate / len(cycles)
    result.update(pulse_valid=True, cycles=len(cycles), duty_percent=100 * positives / periods,
                  period_us=periods * factor, positive_us=positives * factor,
                  negative_us=math.fsum(c[4] for c in cycles) * factor)

    for rising, key, position in ((True, 'rise', 0), (False, 'fall', 1)):
        opposite = crossings(mid, not rising)
        starts = crossings(low if rising else high, rising)
        ends = crossings(high if rising else low, rising)
        durations = []
        for cycle in cycles:
            index = cycle[position][0]
            left = max([i for i, _ in opposite if i < index] or [1])
            right = min([i for i, _ in opposite if i > index] or [len(values) - 1])
            a = [(i, t) for i, t in starts if left <= i <= right]
            b = [(i, t) for i, t in ends if left <= i <= right]
            if len(a) != 1 or len(b) != 1 or b[0][1] - a[0][1] < 2:
                break
            first_index, last_index = a[0][0], b[0][0]
            if last_index < first_index or any(
                    (values[i] < values[i - 1] if rising else values[i] > values[i - 1])
                    for i in range(first_index, last_index + 1)):
                break
            durations.append(b[0][1] - a[0][1])
        if len(durations) == len(cycles) and consistent(durations):
            result[key + '_valid'] = True
            result[key + '_us'] = math.fsum(durations) * factor
    return result


def compare_timing(words, rate, channel, measured):
    expected = timing_reference(words, rate, channel)
    for key, value in expected.items():
        assert key in measured, ('missing timing', key)
        actual = float(measured[key])
        assert math.isfinite(actual), ('nonfinite timing', key)
        if isinstance(value, (bool, int)):
            assert actual == value, (channel, key, actual, value)
        else:
            assert abs(actual - value) <= .00000055 + abs(value) * 2e-12, (
                channel, key, actual, value)
    return expected
