"""Independent SCP1 contract tests; fixtures are constructed from the wire spec."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
READER = ROOT / "scripts" / "settings_blob.py"
EVIDENCE = Path("D:/FPGA_Project/SP2_Settings_Build/python-oracle")
TIMES = (100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000,
         100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000,
         20000000, 50000000, 100000000, 200000000, 500000000)
PROFILES = {5000000: TIMES[8:], 10000000: TIMES[3:],
            25000000: TIMES[3:], 50000000: TIMES}


def fix_crc(data):
    data = bytearray(data)
    data[12:16] = bytes(4)
    struct.pack_into("<I", data, 12, zlib.crc32(data))
    return bytes(data)


def fixture():
    data = bytearray(96)
    struct.pack_into("<4sHHI", data, 0, b"SCP1", 1, 96, 96)
    struct.pack_into("<4Ii3I2i3I", data, 16,
                     100, 50000000, 1, 0, -5000, 31, 0, 6,
                     -3500, 3500, 0, 1000, 1)
    return fix_crc(data)


def mutate(offset, value, fmt="<I"):
    data = bytearray(fixture())
    struct.pack_into(fmt, data, offset, value)
    return fix_crc(data)


def default_fixture(conversion):
    data = bytearray(96)
    struct.pack_into("<4sHHI", data, 0, b"SCP1", 1, 96, 96)
    struct.pack_into("<4Ii3I2i3I", data, 16,
                     200000, conversion, 0, 0, 500, 9, 2, 2,
                     0, 0, 250, 750, 0)
    return fix_crc(data)


if READER.is_file():
    spec = importlib.util.spec_from_file_location("settings_blob", READER)
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
else:
    reader = None


class SettingsBlobTests(unittest.TestCase):
    def setUp(self):
        if reader is None and self._testMethodName != "test_reader_exists":
            self.skipTest("reader not implemented")

    def test_reader_exists(self):
        self.assertIsNotNone(reader, "SCP1 reader has not been implemented")

    def test_standard_crc(self):
        self.assertEqual(zlib.crc32(b"123456789"), 0xCBF43926)

    def test_golden(self):
        data = fixture()
        self.assertEqual(len(data), 96)
        self.assertEqual(data[12:16], bytes.fromhex("6fc6260b"))
        parsed = reader.parse_settings(data)
        self.assertEqual(parsed["settings"], {
            "timebase_ns": 100, "origin_conversion_hz": 50000000,
            "selected_channel": 1, "trigger_channel": 0,
            "trigger_level_mv": -5000, "flags": 31,
            "scale_index": [0, 6], "position_mdiv": [-3500, 3500],
            "cursor": [0, 1000], "language": 1})
        self.assertEqual(parsed["header_crc32"], "0B26C66F")
        self.assertEqual(parsed["crc32"], f"{zlib.crc32(data):08X}")
        self.assertEqual(parsed["sha256"], hashlib.sha256(data).hexdigest().upper())

    def test_default_values_all_profiles(self):
        expected = {"timebase_ns": 200000, "selected_channel": 0,
                    "trigger_channel": 0, "trigger_level_mv": 500,
                    "flags": 9, "scale_index": [2, 2], "position_mdiv": [0, 0],
                    "cursor": [250, 750], "language": 0}
        for conversion in PROFILES:
            with self.subTest(conversion=conversion):
                self.assertEqual(reader.parse_settings(default_fixture(conversion))["settings"],
                                 dict(expected, origin_conversion_hz=conversion))

    def test_every_profile_timebase(self):
        for conversion, supported in PROFILES.items():
            for timebase in TIMES:
                data = bytearray(fixture())
                struct.pack_into("<2I", data, 16, timebase, conversion)
                data = fix_crc(data)
                with self.subTest(conversion=conversion, timebase=timebase):
                    if timebase in supported:
                        self.assertEqual(reader.parse_settings(data)["settings"]["timebase_ns"], timebase)
                    else:
                        with self.assertRaises(ValueError):
                            reader.parse_settings(data)

    def test_recomputed_crc_bad_fields(self):
        bad = [(0, 0), (4, 2), (4, 0x10001), (6, 95), (6, 0x10060),
               (8, 95), (8, 97), (16, 0), (16, 3000), (20, 0), (20, 12500000),
               (24, 2), (24, 0xFFFFFFFF), (28, 2), (32, 5001),
               (32, (-5001) & 0xFFFFFFFF), (32, 0x80000000), (36, 32),
               (36, 0xFFFFFFFF), (40, 7), (44, 7), (48, 3501),
               (48, (-3501) & 0xFFFFFFFF), (52, 3501),
               (52, (-3501) & 0xFFFFFFFF), (56, 1001), (60, 1001),
               (64, 2), (64, 0xFFFFFFFF)]
        for offset, value in bad:
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                reader.parse_settings(mutate(offset, value))

    def test_each_reserved_byte_rejected_with_valid_crc(self):
        for offset in range(68, 96):
            data = bytearray(fixture())
            data[offset] = 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                reader.parse_settings(fix_crc(data))

    def test_each_corrupt_byte_rejected(self):
        for offset in range(96):
            data = bytearray(fixture())
            data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                reader.parse_settings(bytes(data))

    def test_exact_length(self):
        for length in range(96):
            with self.subTest(length=length), self.assertRaises(ValueError):
                reader.parse_settings(fixture()[:length])
        with self.assertRaises(ValueError):
            reader.parse_settings(fixture() + b"\0")

    def test_bytes_input_required(self):
        for value in (None, "SCP1", bytearray(fixture()), memoryview(fixture()), 96):
            with self.subTest(type=type(value)), self.assertRaises(ValueError):
                reader.parse_settings(value)

    def test_valid_endpoints_and_flag_combinations(self):
        for flag in range(32):
            self.assertEqual(reader.parse_settings(mutate(36, flag))["settings"]["flags"], flag)
        for offset, value, key, index in ((32, 5000, "trigger_level_mv", None),
                (48, 3500, "position_mdiv", 0), (52, -3500, "position_mdiv", 1),
                (28, 1, "trigger_channel", None), (64, 0, "language", None)):
            result = reader.parse_settings(mutate(offset, value, "<i"))["settings"][key]
            self.assertEqual(result if index is None else result[index], value)

    def test_cli_stdout_and_file(self):
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=EVIDENCE) as directory:
            source = Path(directory) / "golden.scp"
            output = Path(directory) / "summary.json"
            source.write_bytes(fixture())
            command = [sys.executable, "-B", str(READER), str(source)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), reader.parse_settings(fixture()))
            result = subprocess.run(command + ["--output", str(output)], capture_output=True,
                                    text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), reader.parse_settings(fixture()))

    def test_cli_bad_input_does_not_overwrite_summary(self):
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=EVIDENCE) as directory:
            source, output = Path(directory) / "bad.scp", Path(directory) / "summary.json"
            source.write_bytes(mutate(24, 2))
            output.write_text("keep", encoding="utf-8")
            result = subprocess.run([sys.executable, "-B", str(READER), str(source),
                                     "--output", str(output)], capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("SCP1 validation failed", result.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), "keep")


if __name__ == "__main__":
    unittest.main()
