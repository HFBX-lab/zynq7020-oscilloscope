"""Independent reader/auditor for SCW1 v1 nominal/v2 engineering waveforms.

The contracts are docs/2026-10-24-M5-memory-plan.md and
docs/2026-10-27-M7-calibration-format-design.md. This module uses no
firmware code or external dependencies.  Invalid records raise ValueError.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

HEADER_BYTES = 128
WORD_COUNT = 4096
BLOB_BYTES = HEADER_BYTES + 4 * WORD_COUNT
TIMEBASE_NS = frozenset(multiplier * 10 ** exponent
    for exponent in range(2, 9) for multiplier in (1, 2, 5))
Q16_TOLERANCE = 2 / 65536
F32_SLOPE_MIN, F32_SLOPE_MAX = (struct.unpack('<f', struct.pack('<f', value))[0]
    for value in (.005, .015))


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _calibration(data, version):
    if version == 1:
        _require(data[100:128] == bytes(28), "nonzero reserved header bytes")
        return dict(flags=0, id=0, slope_v_per_code=[0.0, 0.0], offset_v=[0.0, 0.0]), 0
    q8, identity, k1, b1, k2, b2, flags = struct.unpack_from('<II4fI', data, 100)
    mask = flags & 3
    _require(flags & ~0x103 == 0 and mask and flags & 0x100,
        'invalid engineering calibration flags')
    slopes, offsets = [k1, k2], [b1, b2]
    for ch in (0, 1):
        slope, offset = slopes[ch], offsets[ch]
        if mask & (1 << ch):
            _require(math.isfinite(slope) and math.isfinite(offset)
                and F32_SLOPE_MIN <= slope <= F32_SLOPE_MAX,
                'invalid calibration coefficient')
            _require(256 <= -offset/slope <= 768, 'calibration zero code outside bounds')
        else:
            _require(slope == 0 and offset == 0, 'inactive calibration channel has coefficients')
    return dict(flags=flags, id=identity, slope_v_per_code=slopes, offset_v=offsets), q8


def _trigger_q8(meta):
    cal, ch, mv = meta['calibration'], meta['trigger_channel'], meta['trigger_level_mv']
    if cal['flags'] & (1 << ch):
        code = (mv/1000-cal['offset_v'][ch])/cal['slope_v_per_code'][ch]
        return math.floor(max(0, min(1023, code))*256+.5)
    return ((mv+5000)*1023*256+5000)//10000


def parse_record(data: bytes) -> dict:
    """Validate a complete SCW1 blob and return metadata, raw words and hashes.

    crc32 is the actual CRC of all stored bytes, while header_crc32 is the
    integrity value stored at offset 12 (computed with that field zeroed).
    """
    _require(isinstance(data, bytes), "record must be bytes")
    _require(len(data) == BLOB_BYTES, "record length must be 16512")
    magic, version, header_len, total_len, internal_crc = struct.unpack_from("<4sHHII", data)
    _require(magic == b"SCW1", "invalid SCW1 magic")
    _require(version in (1, 2), "unsupported SCW1 version")
    _require(header_len == HEADER_BYTES, "invalid header length")
    _require(total_len == BLOB_BYTES, "invalid total length")
    checked_crc = zlib.crc32(data[:12] + bytes(4) + data[16:])
    _require(internal_crc == checked_crc, "SCW1 integrity CRC mismatch")
    scalar_names = ("count", "rate_hz", "conversion_hz", "filter_kind", "record_id",
        "capture_epoch", "timebase_ns", "view_start_q16", "trigger_crossing_q16",
        "flags", "trigger_channel")
    meta = dict(zip(scalar_names, struct.unpack_from("<11I", data, 16)))
    meta["trigger_level_mv"] = struct.unpack_from("<i", data, 60)[0]
    meta["selected_channel"] = struct.unpack_from("<I", data, 64)[0]
    meta["scale_index"] = list(struct.unpack_from("<2I", data, 68))
    meta["position_mdiv"] = list(struct.unpack_from("<2i", data, 76))
    meta["cursor"] = list(struct.unpack_from("<2I", data, 84))
    unit_code, meta["trigger_after_index"] = struct.unpack_from("<2I", data, 92)
    _require(unit_code == version, "unsupported voltage unit")
    meta['version'], meta['unit_code'] = version, unit_code
    meta['calibration'], meta['trigger_level_q8'] = _calibration(data, version)
    _require(meta["count"] == WORD_COUNT, "unsupported sample count")
    conversion, rate, filtering = (meta[key] for key in ("conversion_hz", "rate_hz", "filter_kind"))
    _require(conversion in (5000000, 10000000, 25000000, 50000000), "unsupported ADC conversion rate")
    cic_rates = {625000, 62500, 6250, 625}
    if conversion >= 25000000:
        cic_rates.add(6250000)
    _require((filtering == 0 and conversion >= 10000000 and rate == conversion)
        or (filtering == 3 and rate in cic_rates), "unsupported conversion/filter/rate combination")
    _require(meta["timebase_ns"] in TIMEBASE_NS, "unsupported physical timebase")
    _require(meta["flags"] & ~63 == 0, "unknown flags")
    _require(meta["trigger_channel"] in (0, 1), "invalid trigger channel")
    _require(meta["selected_channel"] in (0, 1), "invalid selected channel")
    _require(-5000 <= meta["trigger_level_mv"] <= 5000, "trigger level outside nominal voltage range")
    _require(all(value <= 6 for value in meta["scale_index"]), "invalid vertical scale")
    _require(all(-3500 <= value <= 3500 for value in meta["position_mdiv"]), "invalid vertical position")
    _require(all(value <= 1000 for value in meta["cursor"]), "invalid cursor position")
    level_q8 = _trigger_q8(meta)
    if version == 2:
        _require(meta['trigger_level_q8'] == level_q8, 'stored Q8 disagrees with calibration and physical level')

    span = meta["timebase_ns"] * rate / 100000000
    start = meta["view_start_q16"] / 65536
    crossing = meta["trigger_crossing_q16"] / 65536
    _require(math.isfinite(span) and 1 <= span <= 4095, "record window must cover 1..4095 samples")
    _require(0 <= start and start + span <= 4095 + Q16_TOLERANCE, "view window outside record")
    _require(0 <= crossing <= 4095, "trigger crossing outside record")
    raw = data[HEADER_BYTES:]
    words = struct.unpack("<4096I", raw)
    _require(all(word & 0xF800F800 == 0 for word in words), "nonzero reserved ADC word bits")

    after_index = meta["trigger_after_index"]
    if meta["flags"] & 1:
        _require(1 <= after_index < WORD_COUNT, "matched trigger after index outside record")
        shift = 16 * meta["trigger_channel"]
        before_raw, after_raw = ((words[index] >> shift) & 0x7FF
            for index in (after_index - 1, after_index))
        _require((before_raw | after_raw) & 0x400 == 0, "OTR on designated trigger pair")
        before, after = before_raw & 0x3FF, after_raw & 0x3FF
        level = level_q8 / 256
        if meta["flags"] & 2:
            _require(before <= level < after, "designated pair is not a rising Q8 edge")
        else:
            _require(before >= level > after, "designated pair is not a falling Q8 edge")
        actual_crossing = after_index - 1 + (level - before) / (after - before)
        _require(abs(crossing - actual_crossing) <= Q16_TOLERANCE, "stored crossing disagrees with designated edge")
        _require(abs(start - (actual_crossing - .2 * span)) <= Q16_TOLERANCE, "matched trigger is not aligned at 20 percent")
    else:
        _require(after_index == 0, "unmatched record must have zero trigger after index")

    return {"meta": meta, "words": words, "raw_data": raw,
        "crc32": f"{zlib.crc32(data):08X}", "header_crc32": f"{internal_crc:08X}",
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "payload_crc32": f"{zlib.crc32(raw):08X}",
        "view_start": start, "crossing": crossing}


def main(argv=None):
    parser = argparse.ArgumentParser(description="Independently validate an SCW1 waveform record")
    parser.add_argument("record", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        record = parse_record(args.record.read_bytes())
    except (OSError, ValueError) as error:
        parser.exit(1, f"SCW1 validation failed: {error}\n")
    summary = {key: value for key, value in record.items() if key not in ("raw_data", "words")}
    formatted = json.dumps(summary, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(formatted, encoding="utf-8")
    else:
        print(formatted, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
