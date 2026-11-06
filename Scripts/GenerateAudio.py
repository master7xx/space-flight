"""Synthesize original, reproducible game effects; no downloaded samples.

Run with Python 3, NumPy and SciPy. All source clips share an RMS target and
peak ceiling; the game applies a separate, lower gain to its continuous engine.
"""
from pathlib import Path
import wave

import numpy as np
from scipy.signal import butter, sosfilt

RATE = 48_000
OUT = Path(__file__).resolve().parents[1] / "Art" / "Audio"
RNG = np.random.default_rng(204810)


def time(seconds):
    return np.arange(round(seconds * RATE), dtype=np.float64) / RATE


def env(t, attack, decay, power=1.0):
    return np.minimum(t / attack, 1.0) * np.exp(-t / decay) ** power


def tone(t, frequency, end_frequency=None):
    end_frequency = frequency if end_frequency is None else end_frequency
    duration = max(t[-1], 1 / RATE)
    phase = 2 * np.pi * (frequency * t + 0.5 * (end_frequency - frequency) * t * t / duration)
    return np.sin(phase)


def filtered_noise(t, low, high):
    white = RNG.standard_normal(len(t))
    sos = butter(3, (low, high), btype="bandpass", fs=RATE, output="sos")
    noise = sosfilt(sos, white)
    return noise / max(np.std(noise), 1e-6)


def chime(seconds, pitches, gap=0.085):
    t = time(seconds)
    out = np.zeros_like(t)
    for i, hz in enumerate(pitches):
        local = np.maximum(0.0, t - i * gap)
        e = env(local, 0.008, 0.115) * (t >= i * gap)
        out += e * (tone(local, hz) + 0.18 * tone(local, hz * 2.01))
    return out


def sweep(seconds, a, b, decay=0.11):
    t = time(seconds)
    return env(t, 0.006, decay) * (tone(t, a, b) + 0.22 * tone(t, a * 2, b * 2))


def make_sounds():
    sounds = {}
    t = time(3.0)
    modulation = 0.8 + 0.12 * np.cos(2 * np.pi * t / 3) + 0.08 * np.cos(4 * np.pi * t / 3)
    sounds["engine"] = modulation * (tone(t, 96) + 0.31 * tone(t, 192) + 0.11 * tone(t, 288))
    sounds["launch"] = chime(0.49, [330, 440, 554], 0.10)
    t = time(0.22)
    sounds["laser"] = env(t, .004, .075) * (tone(t, 1680, 610) + .20 * filtered_noise(t, 800, 3600))
    t = time(0.22)
    sounds["impact"] = env(t, .003, .055) * (.48 * filtered_noise(t, 380, 3400) + .8 * tone(t, 330, 125))
    for label, length, low, drop in [("small", .28, 230, 115), ("medium", .42, 165, 75), ("large", .56, 120, 48)]:
        t = time(length)
        attack = env(t, .003, .075) * .50 * filtered_noise(t, 180, 3800)
        body = env(t, .008, length * .34) * (tone(t, low, drop) + .27 * filtered_noise(t, 80, 850))
        sounds["explosion_" + label] = attack + body
    sounds["isotope"] = chime(.31, [720, 1080], .065)
    sounds["upgrade"] = chime(.53, [392, 494, 587, 784], .09)
    sounds["denied"] = chime(.25, [300, 225], .07)
    sounds["tree_open"] = sweep(.19, 410, 680)
    sounds["tree_close"] = sweep(.19, 680, 410)
    sounds["mode_on"] = chime(.29, [440, 660], .07)
    sounds["mode_off"] = chime(.29, [660, 440], .07)
    sounds["pause"] = chime(.25, [520, 390], .06)
    sounds["resume"] = chime(.25, [390, 520], .06)
    sounds["ui_tick"] = sweep(.11, 580, 670, .045)
    sounds["stress"] = chime(.42, [330, 440, 330], .105)
    # Independent timbres, rather than pitch-shifting the same file, keep
    # repeated volleys and asteroid impacts from sounding like a loop.
    for variant, start, finish, noise_mix in [(2, 1320, 490, .12), (3, 1900, 750, .16)]:
        t = time(.22)
        sounds[f"laser_{variant}"] = env(t, .006, .070 + .006 * variant) * (
            tone(t, start, finish) + .18 * tone(t, start * .51, finish * .53) +
            noise_mix * filtered_noise(t, 600, 3300))
    for variant, start, finish, noise_mix in [(2, 275, 105, .42), (3, 420, 155, .36)]:
        t = time(.22)
        sounds[f"impact_{variant}"] = env(t, .004, .065) * (
            noise_mix * filtered_noise(t, 280 + variant * 70, 2900) + .8 * tone(t, start, finish))
    for label, length, low, drop in [("small", .28, 230, 115), ("medium", .42, 165, 75), ("large", .56, 120, 48)]:
        for variant, tuning in [(2, .88), (3, 1.13)]:
            t = time(length)
            transient = env(t, .004, .060 + .008 * variant) * .43 * filtered_noise(t, 160, 2900 + variant * 350)
            body = env(t, .009, length * .34) * (
                tone(t, low * tuning, drop * tuning) + .22 * filtered_noise(t, 80, 700 + variant * 70))
            sounds[f"explosion_{label}_{variant}"] = transient + body
    return sounds


def normalize(x):
    # Soft saturation bounds crest factor before matching a -24 dBFS RMS target.
    x = np.tanh(x * 1.5)
    x *= 10 ** (-24 / 20) / max(np.sqrt(np.mean(x * x)), 1e-12)
    peak = np.max(np.abs(x))
    if peak > 0.50:
        x = np.clip(x, -0.50, 0.50)
    n = min(240, len(x) // 4)
    fade = np.sin(np.linspace(0, np.pi / 2, n)) ** 2
    x[:n] *= fade
    x[-n:] *= fade[::-1]
    return np.round(x * 32767).astype("<i2")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, source in make_sounds().items():
        data = normalize(source)
        with wave.open(str(OUT / (name + ".wav")), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            wav.writeframes(data.tobytes())
        values = data.astype(np.float64) / 32768
        print(f"{name:18s} {len(values)/RATE:.2f}s rms={20*np.log10(np.sqrt(np.mean(values**2))):.1f}dBFS peak={20*np.log10(np.max(np.abs(values))):.1f}dBFS")


if __name__ == "__main__":
    main()
