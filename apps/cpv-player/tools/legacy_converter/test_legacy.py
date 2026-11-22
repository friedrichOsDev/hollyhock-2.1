#!/usr/bin/env python3
"""Tests for the isolated Python port of the original CPV converter."""

from __future__ import annotations

import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("legacy_cpv_converter", HERE / "convert.py")
legacy = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = legacy
SPEC.loader.exec_module(legacy)


def decode_legacy_packets(packet_bytes: bytes, previous: bytearray, pixel_count: int) -> bytearray:
    """Small test decoder matching the old C player's packet interpretation."""
    position = 0
    pixel_position = 0
    while position < len(packet_bytes):
        first = packet_bytes[position]
        if first & 0x80:
            if first & 0x40:
                if position + 2 > len(packet_bytes):
                    raise AssertionError("truncated long SKIP")
                length = int.from_bytes(packet_bytes[position:position + 2], "big") & 0x3FFF
                position += 2
            else:
                length = first & 0x3F
                position += 1
            pixel_position += length
        else:
            if first & 0x40:
                if position + 3 > len(packet_bytes):
                    raise AssertionError("truncated long DATA")
                length = int.from_bytes(packet_bytes[position:position + 2], "big") & 0x3FFF
                colour = packet_bytes[position + 2]
                position += 3
            else:
                if position + 2 > len(packet_bytes):
                    raise AssertionError("truncated short DATA")
                length = first & 0x3F
                colour = packet_bytes[position + 1]
                position += 2
            previous[pixel_position:pixel_position + length] = bytes((colour,)) * length
            pixel_position += length
        if length <= 0 or pixel_position > pixel_count:
            raise AssertionError("invalid legacy packet coverage")
    if pixel_position != pixel_count:
        raise AssertionError(
            f"frame covered {pixel_position} pixels, expected {pixel_count}"
        )
    return previous


class LegacyPaletteTests(unittest.TestCase):
    def test_seed_image_matches_original_36_rgb888_entries(self):
        seed = legacy.create_seed_palette()
        self.assertEqual(len(seed), 108)
        self.assertEqual(seed[:9], bytes((127, 0, 0, 255, 0, 0, 255, 127, 127)))
        self.assertEqual(seed[-9:], bytes((64, 64, 64, 128, 128, 128, 191, 191, 191)))

    def test_header_matches_original_packed_layout(self):
        header = legacy.build_legacy_header(80, 45, 5)
        self.assertEqual(header, b"CP" + bytes((80, 45, 5, 0)))
        self.assertEqual(len(header), 6)


class LegacyRLETests(unittest.TestCase):
    def test_mixed_skip_and_data_packet_sequence(self):
        import numpy as np
        previous = np.array([0, 0, 0, 1, 1, 1], dtype=np.uint8)
        current = np.array([0, 1, 1, 1, 2, 2], dtype=np.uint8)
        packets = legacy.encode_frame_packets(current, previous)
        self.assertEqual(packets, bytes((0x81, 0x02, 0x01, 0x81, 0x02, 0x02)))
        decoded = decode_legacy_packets(packets, bytearray(previous.tobytes()), 6)
        self.assertEqual(decoded, bytearray(current.tobytes()))

    def test_long_skip_and_long_data_boundaries(self):
        import numpy as np
        previous = np.zeros(64, dtype=np.uint8)
        current_skip = np.zeros(64, dtype=np.uint8)
        self.assertEqual(legacy.encode_frame_packets(current_skip, previous), b"\xC0\x40")

        current_data = np.full(64, 7, dtype=np.uint8)
        self.assertEqual(
            legacy.encode_frame_packets(current_data, previous),
            b"\x40\x40\x07",
        )

    def test_packet_stream_round_trips_first_frame_and_p_frames(self):
        import numpy as np
        previous = np.zeros(100, dtype=np.uint8)
        frames = [
            np.array([(i // 5) % 12 for i in range(100)], dtype=np.uint8),
            np.array([(i // 7) % 12 for i in range(100)], dtype=np.uint8),
            np.zeros(100, dtype=np.uint8),
        ]
        for frame in frames:
            packets = legacy.encode_frame_packets(frame, previous)
            decode_legacy_packets(packets, bytearray(previous.tobytes()), 100)
            rebuilt = bytearray(previous.tobytes())
            decode_legacy_packets(packets, rebuilt, 100)
            self.assertEqual(rebuilt, bytearray(frame.tobytes()))
            previous[:] = frame


class LegacyPaletteMappingTests(unittest.TestCase):
    def test_exact_rgb565_palette_entry_maps_to_first_matching_index(self):
        import numpy as np
        palette = np.array([0xF800, 0x07E0, 0x001F] + [0] * 253, dtype=np.uint16)
        mapped = legacy.map_frame_to_indices(bytes((248, 0, 0, 0, 252, 0, 0, 0, 248)), palette)
        self.assertEqual(mapped.tolist(), [0, 1, 2])


class LegacyIntegrationTests(unittest.TestCase):
    def test_ffmpeg_conversion_produces_legacy_cp_file_and_decodable_frames(self):
        import numpy as np
        if not shutil.which("ffmpeg") or not shutil.which("ffprobe"):
            self.skipTest("FFmpeg/FFprobe is not installed")

        with tempfile.TemporaryDirectory(prefix="legacy_cpv_test_") as temp:
            root = Path(temp)
            source = root / "testsrc.mp4"
            output = root / "testsrc.mp4.cpv"
            subprocess.run(
                [
                    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=5:duration=2",
                    "-c:v", "mpeg4", "-q:v", "3", str(source),
                ],
                check=True,
            )
            result = legacy.convert_video(source, output, max_size=80, fps=5)
            data = output.read_bytes()
            self.assertEqual(data[:2], b"CP")
            magic, width, height, fps_flags, reserved = legacy.LEGACY_HEADER.unpack_from(data)
            self.assertEqual(magic, b"CP")
            self.assertEqual((width, height), (64, 48))
            self.assertEqual(fps_flags, 5)
            self.assertEqual(reserved, 0)
            self.assertEqual(result["frames"], 10)
            self.assertEqual(len(data[6:6 + legacy.PALETTE_BYTES]), 512)

            pixel_count = width * height
            previous = bytearray(pixel_count)
            offset = legacy.FILE_DATA_OFFSET
            frame_count = 0
            while offset < len(data):
                self.assertLessEqual(offset + 2, len(data))
                packet_size = int.from_bytes(data[offset:offset + 2], "big")
                offset += 2
                packet_end = offset + packet_size
                self.assertLessEqual(packet_end, len(data))
                decode_legacy_packets(data[offset:packet_end], previous, pixel_count)
                offset = packet_end
                frame_count += 1

            self.assertEqual(offset, len(data))
            self.assertEqual(frame_count, 10)


if __name__ == "__main__":
    unittest.main()
