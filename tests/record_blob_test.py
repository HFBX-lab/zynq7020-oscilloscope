"""Independent SCW1 format contract tests, without importing the C implementation."""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
PARSER = ROOT / "scripts" / "record_blob.py"
GOLDEN = Path("D:/FPGA_Project/SP2_Memory_Build/codec/golden.scw")


def seal(data):
    data = bytearray(data)
    data[12:16] = bytes(4)
    struct.pack_into("<I", data, 12, zlib.crc32(data))
    return bytes(data)


def change(data, offset, value, fmt="I"):
    data = bytearray(data)
    struct.pack_into("<" + fmt, data, offset, value)
    return seal(data)


class RecordBlobTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.golden = GOLDEN.read_bytes()
        if PARSER.exists():
            spec = importlib.util.spec_from_file_location("record_blob", PARSER)
            cls.module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.module)
        else:
            cls.module = None

    def parse(self, data):
        self.assertIsNotNone(self.module, "SCW1 parser implementation is missing")
        return self.module.parse_record(data)

    def test_golden_contract(self):
        result = self.parse(self.golden)
        self.assertEqual(result["sha256"], "27599725E14EA011467697C24922DE714D53F7BA4CF332C92545E3F9308CDBAF")
        self.assertEqual(result["crc32"], f"{zlib.crc32(self.golden):08X}")
        self.assertEqual(result["header_crc32"], f"{struct.unpack_from('<I', self.golden, 12)[0]:08X}")
        self.assertEqual(result["payload_crc32"], f"{zlib.crc32(self.golden[128:]):08X}")
        self.assertEqual(result["raw_data"], self.golden[128:])
        self.assertIsInstance(result["words"], tuple)
        self.assertEqual(len(result["words"]), 4096)
        self.assertEqual(result["meta"], dict(count=4096, rate_hz=6250000,
            conversion_hz=50000000, filter_kind=3, record_id=0x12345678,
            capture_epoch=0x90ABCDEF, timebase_ns=10000, view_start_q16=0,
            trigger_crossing_q16=0, flags=42, trigger_channel=1,
            trigger_level_mv=500, selected_channel=0, scale_index=[1, 6],
            position_mdiv=[-3500, 3500], cursor=[0, 1000], trigger_after_index=0,
            version=1, unit_code=1, trigger_level_q8=0,
            calibration=dict(flags=0, id=0, slope_v_per_code=[0.0, 0.0], offset_v=[0.0, 0.0])))

    def test_invalid_headers_and_metadata(self):
        cases = [(0, 0, "I"), (4, 2, "H"), (6, 127, "H"),
            (8, 16511, "I"), (16, 4095, "I"), (20, 6250001, "I"),
            (24, 40000000, "I"), (28, 99, "I"), (40, 1234, "I"),
            (40, 100000000, "I"), (44, 4095 << 16, "I"),
            (48, 4096 << 16, "I"), (52, 64, "I"), (56, 2, "I"),
            (60, -5001, "i"), (60, 5001, "i"), (64, 2, "I"),
            (68, 7, "I"), (72, 7, "I"), (76, -3501, "i"),
            (80, 3501, "i"), (84, 1001, "I"), (88, 1001, "I"),
            (92, 0, "I"), (96, 1, "I"), (100, 1, "I"),
            (124, 1, "I"), (128, 1 << 11, "I"),
            (128, 1 << 27, "I")]
        for offset, value, fmt in cases:
            with self.subTest(offset=offset, value=value):
                with self.assertRaises(ValueError):
                    self.parse(change(self.golden, offset, value, fmt))

    def test_crc_and_exact_length(self):
        corrupted = bytearray(self.golden)
        corrupted[-1] ^= 1
        for value in (bytes(corrupted), self.golden[:-1], self.golden + b"x", b""):
            with self.subTest(length=len(value)):
                with self.assertRaises(ValueError):
                    self.parse(value)

    def matched_blob(self, rising=True, channel=1):
        data = bytearray(self.golden)
        level_q8 = ((5500 * 1023 * 256 + 5000) // 10000) / 256
        before, after = (562, 564) if rising else (564, 562)
        crossing = 999 + (level_q8 - before) / (after - before)
        q16 = int(crossing * 65536 + .5)
        struct.pack_into("<III", data, 44, q16 - 125 * 65536, q16, 41 | (2 if rising else 0))
        struct.pack_into("<I", data, 56, channel)
        struct.pack_into("<I", data, 96, 1000)
        for index, sample in ((999, before), (1000, after)):
            struct.pack_into("<I", data, 128 + 4 * index, sample << (16 * channel))
        return seal(data)

    def test_matched_rising_falling_and_both_channels(self):
        for rising in (True, False):
            for channel in (0, 1):
                with self.subTest(rising=rising, channel=channel):
                    value = self.parse(self.matched_blob(rising, channel))
                    self.assertAlmostEqual(value["crossing"] - value["view_start"], 125)

    def test_matched_corruption_with_valid_crc(self):
        blob = self.matched_blob()
        for offset, value in ((96, 0), (96, 4096), (96, 999),
                (52, 41), (48, 1000 << 16), (44, 0),
                (128 + 4 * 999, (562 | 1024) << 16),
                (128 + 4 * 1000, (564 | 1024) << 16),
                (128 + 4 * 1000, 562 << 16)):
            with self.subTest(offset=offset, value=value):
                with self.assertRaises(ValueError):
                    self.parse(change(blob, offset, value))

    def test_nontrigger_otr_and_nonzero_unmatched_crossing_are_valid(self):
        self.parse(change(self.golden, 48, 123 << 16))
        self.parse(change(self.matched_blob(), 128 + 4 * 2000, 0x04000400))

    def test_every_supported_rate_profile(self):
        for conversion in (5000000, 10000000, 25000000, 50000000):
            profiles = [(3, rate) for rate in (625000, 62500, 6250, 625)]
            if conversion >= 10000000:
                profiles.append((0, conversion))
            if conversion >= 25000000:
                profiles.append((3, 6250000))
            for filtering, rate in profiles:
                blob = change(change(change(self.golden, 24, conversion), 28, filtering), 20, rate)
                timebase = next(t for t in sorted(self.module.TIMEBASE_NS) if 1 <= t * rate / 100000000 <= 4095)
                with self.subTest(conversion=conversion, rate=rate, filtering=filtering):
                    self.parse(change(blob, 40, timebase))

    def test_all_twenty_one_timebases(self):
        timebases = [multiplier * 10 ** exponent for exponent in range(2, 9) for multiplier in (1, 2, 5)]
        for timebase in timebases:
            rate, filtering = next((rate, filtering) for rate, filtering in
                ((50000000, 0), (6250000, 3), (625000, 3), (62500, 3), (6250, 3), (625, 3))
                if 1 <= timebase * rate / 100000000 <= 4095)
            blob = change(change(change(self.golden, 40, timebase), 20, rate), 28, filtering)
            with self.subTest(timebase=timebase):
                self.parse(blob)

    def test_unsupported_profile_combinations(self):
        for conversion, filtering, rate in ((5000000, 0, 5000000),
                (5000000, 3, 6250000), (10000000, 3, 6250000),
                (25000000, 0, 50000000), (50000000, 3, 50000000),
                (50000000, 1, 6250000)):
            blob = change(change(change(self.golden, 24, conversion), 28, filtering), 20, rate)
            with self.subTest(conversion=conversion, filtering=filtering, rate=rate):
                with self.assertRaises(ValueError):
                    self.parse(blob)

    def test_exact_crossing_and_q16_tolerance(self):
        blob = self.matched_blob()
        crossing = struct.unpack_from("<I", blob, 48)[0]
        start = struct.unpack_from("<I", blob, 44)[0]
        for offset, original in ((44, start), (48, crossing)):
            for delta in (-2, 2):
                self.parse(change(blob, offset, original + delta))
            for delta in (-3, 3):
                with self.assertRaises(ValueError):
                    self.parse(change(blob, offset, original + delta))
        exact = change(change(blob, 60, -5000, "i"), 128 + 4 * 999, 0)
        exact = change(exact, 128 + 4 * 1000, 1 << 16)
        exact = change(change(exact, 48, 999 << 16), 44, 874 << 16)
        self.parse(exact)
        # An exact crossing on the before sample still belongs to the stated pair.
        with self.assertRaises(ValueError):
            self.parse(change(exact, 96, 999))

    def test_window_and_voltage_endpoints(self):
        for level in (-5000, 5000):
            self.parse(change(self.golden, 60, level, "i"))
        self.parse(change(self.golden, 48, 4095 << 16))
        end = change(self.golden, 44, (4095 - 625) << 16)
        self.parse(end)
        self.parse(change(end, 44, ((4095 - 625) << 16) + 2))
        with self.assertRaises(ValueError):
            self.parse(change(end, 44, ((4095 - 625) << 16) + 3))
        with self.assertRaises(ValueError):
            self.parse(change(change(self.golden, 20, 625), 40, 100))

    def test_cli_omits_payload_and_words(self):
        self.assertIsNotNone(self.module, "SCW1 parser implementation is missing")
        with tempfile.TemporaryDirectory(dir="D:/FPGA_Project/SP2_Memory_Build") as folder:
            output = Path(folder) / "summary.json"
            result = subprocess.run([sys.executable, str(PARSER), str(GOLDEN), "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            value = json.loads(output.read_text(encoding="utf-8"))
            self.assertNotIn("words", value)
            self.assertNotIn("raw_data", value)
            self.assertEqual(value["sha256"], hashlib.sha256(self.golden).hexdigest().upper())

    def test_cli_rejects_corruption_without_summary(self):
        self.assertIsNotNone(self.module, "SCW1 parser implementation is missing")
        with tempfile.TemporaryDirectory(dir="D:/FPGA_Project/SP2_Memory_Build") as folder:
            record = Path(folder) / "bad.scw"
            output = Path(folder) / "bad.json"
            record.write_bytes(self.golden[:-1])
            result = subprocess.run([sys.executable, str(PARSER), str(record), "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("SCW1 validation failed", result.stderr)
            self.assertFalse(output.exists())

    def v2_blob(self, mask=3, matched=False, channel=1, rising=True):
        data = bytearray(self.golden)
        struct.pack_into('<H', data, 4, 2)
        struct.pack_into('<I', data, 92, 2)
        k = [struct.unpack('<f', struct.pack('<f', v))[0] for v in (.009, .011)]
        b = [struct.unpack('<f', struct.pack('<f', v))[0] for v in (-4.5, -5.7)]
        level = (.5-b[channel])/k[channel] if mask & (1 << channel) else 5500*1023/10000
        q8 = math.floor(max(0, min(1023, level))*256+.5)
        struct.pack_into('<II4fI', data, 100, q8, 0x8b012345,
            k[0] if mask & 1 else 0, b[0] if mask & 1 else 0,
            k[1] if mask & 2 else 0, b[1] if mask & 2 else 0, 0x100 | mask)
        struct.pack_into('<I', data, 56, channel)
        if matched:
            lo = q8 // 256
            before, after = (lo, lo+1) if rising else (lo+1, lo)
            crossing = 999 + (q8/256-before)/(after-before)
            q16 = int(crossing*65536+.5)
            struct.pack_into('<III', data, 44, q16-125*65536, q16, 41 | (2 if rising else 0))
            struct.pack_into('<I', data, 96, 1000)
            for i, sample in ((999, before), (1000, after)):
                struct.pack_into('<I', data, 128+4*i, sample << (16*channel))
        return seal(data)

    def test_v2_profile_and_partial_roundtrip(self):
        for mask in (1, 2, 3):
            parsed = self.parse(self.v2_blob(mask))
            self.assertEqual(parsed['meta']['version'], 2)
            self.assertEqual(parsed['meta']['unit_code'], 2)
            cal = parsed['meta']['calibration']
            self.assertEqual(cal['flags'], 0x100 | mask)
            self.assertEqual(cal['id'], 0x8b012345)
            self.parse(change(self.v2_blob(mask), 104, 0))

    def test_v2_matched_profiles_channels_and_directions(self):
        for mask in (1, 2, 3):
            for ch in (0, 1):
                for rising in (False, True):
                    value = self.parse(self.v2_blob(mask, True, ch, rising))
                    self.assertAlmostEqual(value['crossing']-value['view_start'], 125)

    def test_v2_rejects_profile_q8_and_matched_corruption(self):
        base = self.v2_blob()
        cases = [(4, 3, 'H'), (92, 1, 'I'), (100, 0, 'I'), (100, 0x40000, 'I'),
            (108, float('nan'), 'f'), (108, float('inf'), 'f'), (108, 0, 'f'),
            (108, -.01, 'f'), (108, .1, 'f'), (112, 0, 'f'),
            (116, float('-inf'), 'f'), (120, float('nan'), 'f'),
            (124, 0, 'I'), (124, 3, 'I'), (124, 0x100, 'I'), (124, 0x107, 'I')]
        for off, value, fmt in cases:
            with self.subTest(offset=off, value=value):
                with self.assertRaises(ValueError):
                    self.parse(change(base, off, value, fmt))
        with self.assertRaises(ValueError):
            self.parse(change(self.v2_blob(1), 116, .01, 'f'))
        matched = self.v2_blob(matched=True)
        for off, value in ((100, struct.unpack_from('<I', matched, 100)[0]+1),
                (96, 999), (48, 1000 << 16), (44, 0),
                (128+4*999, (1024 | 563) << 16)):
            with self.subTest(offset=off, value=value):
                with self.assertRaises(ValueError):
                    self.parse(change(matched, off, value))

    def test_v2_voltage_endpoint_clamps(self):
        for channel in (0, 1):
            for mv in (-5000, 5000):
                data = self.v2_blob(channel=channel)
                k, b = struct.unpack_from('<2f', data, 108+8*channel)
                q8 = math.floor(max(0, min(1023, (mv/1000-b)/k))*256+.5)
                data = change(change(data, 60, mv, 'i'), 100, q8)
                self.assertEqual(self.parse(data)['meta']['trigger_level_q8'], q8)

    def test_c_produced_v2_fixture(self):
        data = Path('D:/FPGA_Project/SP2_Calibration_Build/record-v2/golden-v2.scw').read_bytes()
        parsed = self.parse(data)
        self.assertEqual(parsed['meta']['record_id'], 12)
        self.assertEqual(parsed['meta']['capture_epoch'], 17)
        self.assertEqual(parsed['meta']['calibration']['flags'], 0x103)
        self.assertEqual(parsed['meta']['calibration']['id'], 0x8b012345)
        self.assertEqual(parsed['meta']['trigger_level_q8'], struct.unpack_from('<I', data, 100)[0])
        self.assertEqual(parsed['raw_data'], b''.join(struct.pack('<I', (i & 1023) | (((i*3) & 1023) << 16)) for i in range(4096)))


if __name__ == "__main__":
    unittest.main()
