"""Checks the source audio shipped with the game (run with python -m unittest)."""

from pathlib import Path
import unittest
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SOUND_DIR = ROOT / "Art" / "Audio"
SOUNDS = (
    "engine", "launch", "laser", "impact", "explosion_small",
    "explosion_medium", "explosion_large", "isotope", "upgrade", "denied",
    "tree_open", "tree_close", "mode_on", "mode_off", "pause", "resume",
    "ui_tick", "stress",
)
COMBAT_SOUNDS = ("laser", "impact", "explosion_small", "explosion_medium", "explosion_large")
SOUNDS += tuple(f"{name}_{variant}" for name in COMBAT_SOUNDS for variant in (2, 3))


class AudioAssetsTest(unittest.TestCase):
    def test_combat_variants_are_distinct(self):
        for name in COMBAT_SOUNDS:
            with self.subTest(sound=name):
                samples = [(SOUND_DIR / f"{name}{suffix}.wav").read_bytes() for suffix in ("", "_2", "_3")]
                self.assertEqual(len(set(samples)), 3)

    def test_all_sources_are_mono_safe_and_loudness_matched(self):
        for name in SOUNDS:
            with self.subTest(sound=name):
                path = SOUND_DIR / (name + ".wav")
                self.assertTrue(path.is_file(), str(path))
                with wave.open(str(path), "rb") as wav:
                    self.assertEqual(wav.getnchannels(), 1)
                    self.assertEqual(wav.getsampwidth(), 2)
                    self.assertEqual(wav.getframerate(), 48000)
                    samples = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").astype(np.float64) / 32768
                self.assertGreater(len(samples), 1000)
                peak = np.max(np.abs(samples))
                rms = np.sqrt(np.mean(samples ** 2))
                self.assertLessEqual(peak, 0.502, "leave at least 6 dB of peak headroom")
                self.assertGreater(rms, 0.04, "avoid near-silent assets")
                self.assertLess(rms, 0.075, "all source effects share a comparable RMS level")
                self.assertLess(abs(samples[0]), 0.003)
                self.assertLess(abs(samples[-1]), 0.003)
                if name == "engine":
                    self.assertGreater(len(samples), 48000)


if __name__ == "__main__":
    unittest.main()
