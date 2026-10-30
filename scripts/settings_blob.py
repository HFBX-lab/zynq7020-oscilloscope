"""Independent SCP1 settings reader, defined by the M5-B wire-format plan.

No firmware implementation or external dependencies are used. Invalid input
raises ValueError; only a fully validated settings dictionary is returned.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

BLOB_BYTES = 96
TIMEBASE_NS = (100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000,
               100000, 200000, 500000, 1000000, 2000000, 5000000,
               10000000, 20000000, 50000000, 100000000, 200000000, 500000000)
PROFILE_TIMEBASE_NS = {5000000: frozenset(TIMEBASE_NS[8:]),
                       10000000: frozenset(TIMEBASE_NS[3:]),
                       25000000: frozenset(TIMEBASE_NS[3:]),
                       50000000: frozenset(TIMEBASE_NS)}


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def parse_settings(data: bytes) -> dict:
    """Validate exactly one complete 96-byte SCP1 settings blob.

    crc32 covers every stored byte. header_crc32 is the validated integrity
    CRC, computed with bytes 12..15 replaced by zero. All hashes are uppercase.
    """
    _require(isinstance(data, bytes), "settings must be bytes")
    _require(len(data) == BLOB_BYTES, "settings length must be 96")
    magic, version, size, total, stored_crc = struct.unpack_from("<4sHHII", data)
    _require(magic == b"SCP1", "invalid SCP1 magic")
    _require(version == 1, "unsupported SCP1 version")
    _require(size == BLOB_BYTES, "invalid SCP1 header size")
    _require(total == BLOB_BYTES, "invalid SCP1 total length")
    checked_crc = zlib.crc32(data[:12] + bytes(4) + data[16:])
    _require(checked_crc == stored_crc, "SCP1 integrity CRC mismatch")
    _require(data[68:] == bytes(28), "nonzero reserved settings bytes")

    settings = dict(zip(("timebase_ns", "origin_conversion_hz", "selected_channel",
                         "trigger_channel"), struct.unpack_from("<4I", data, 16)))
    settings["trigger_level_mv"] = struct.unpack_from("<i", data, 32)[0]
    settings["flags"] = struct.unpack_from("<I", data, 36)[0]
    settings["scale_index"] = list(struct.unpack_from("<2I", data, 40))
    settings["position_mdiv"] = list(struct.unpack_from("<2i", data, 48))
    settings["cursor"] = list(struct.unpack_from("<2I", data, 56))
    settings["language"] = struct.unpack_from("<I", data, 64)[0]

    conversion = settings["origin_conversion_hz"]
    _require(conversion in PROFILE_TIMEBASE_NS, "unsupported origin conversion rate")
    _require(settings["timebase_ns"] in PROFILE_TIMEBASE_NS[conversion],
             "physical timebase unsupported by origin conversion profile")
    _require(settings["selected_channel"] in (0, 1), "invalid selected channel")
    _require(settings["trigger_channel"] in (0, 1), "invalid trigger channel")
    _require(-5000 <= settings["trigger_level_mv"] <= 5000, "invalid trigger level")
    _require(settings["flags"] & ~31 == 0, "unknown settings flags")
    _require(all(value <= 6 for value in settings["scale_index"]), "invalid vertical scale")
    _require(all(-3500 <= value <= 3500 for value in settings["position_mdiv"]),
             "invalid vertical position")
    _require(all(value <= 1000 for value in settings["cursor"]), "invalid cursor position")
    _require(settings["language"] in (0, 1), "unsupported settings language")
    return {"settings": settings, "crc32": f"{zlib.crc32(data):08X}",
            "header_crc32": f"{checked_crc:08X}",
            "sha256": hashlib.sha256(data).hexdigest().upper()}


def main(argv=None):
    parser = argparse.ArgumentParser(description="Independently validate an SCP1 settings blob")
    parser.add_argument("settings", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        summary = parse_settings(args.settings.read_bytes())
        formatted = json.dumps(summary, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.write_text(formatted, encoding="utf-8")
        else:
            print(formatted, end="")
    except (OSError, ValueError) as error:
        parser.exit(1, f"SCP1 validation failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
