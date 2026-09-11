# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

import argparse
import csv
import json
import math
import sys
import wave
from pathlib import Path

import numpy as np


def robust_limit(values: np.ndarray, multiplier: float, factor: float, floor: float = 0.0) -> float:
    median = float(np.median(values))
    mad = float(np.median(np.abs(values - median)))
    return max(median + multiplier * 1.4826 * mad, median * factor, floor)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a captured 200 Hz stereo loopback WAV")
    parser.add_argument("wav", type=Path)
    parser.add_argument("--frequency", type=float, default=200.0)
    parser.add_argument("--window-ms", type=float, default=100.0)
    parser.add_argument("--skip-seconds", type=float, default=2.0)
    args = parser.parse_args()

    metrics = [[], []]
    clipped = [0, 0]
    total_frames = 0

    with wave.open(str(args.wav), "rb") as source:
        channels = source.getnchannels()
        sample_rate = source.getframerate()
        sample_width = source.getsampwidth()
        declared_frames = source.getnframes()
        if channels != 2 or sample_width != 4:
            raise RuntimeError(f"Expected stereo 32-bit PCM, got {channels} channels / {sample_width * 8} bits")

        window_frames = int(round(sample_rate * args.window_ms / 1000.0))
        if window_frames < 32:
            raise RuntimeError("Analysis window is too short")
        frame_index = np.arange(window_frames, dtype=np.float64)
        angle = 2.0 * math.pi * args.frequency * frame_index / sample_rate
        sine = np.sin(angle)
        cosine = np.cos(angle)

        while True:
            raw = source.readframes(window_frames)
            if not raw:
                break
            samples = np.frombuffer(raw, dtype="<i4")
            frames = samples.size // channels
            if frames != window_frames:
                total_frames += frames
                break
            stereo = samples.reshape(frames, channels).astype(np.float64)

            for channel in range(2):
                values = stereo[:, channel]
                dc = float(np.mean(values))
                centered = values - dc
                sin_coefficient = 2.0 * float(np.dot(centered, sine)) / frames
                cos_coefficient = 2.0 * float(np.dot(centered, cosine)) / frames
                amplitude = math.hypot(sin_coefficient, cos_coefficient)
                fitted = dc + sin_coefficient * sine + cos_coefficient * cosine
                residual = values - fitted
                residual_rms = float(np.sqrt(np.mean(residual * residual)))
                residual_peak = float(np.max(np.abs(residual)))
                step_peak = float(np.max(np.abs(np.diff(values))))
                denominator = max(amplitude, 1.0)
                metrics[channel].append(
                    (
                        total_frames / sample_rate,
                        amplitude,
                        math.atan2(cos_coefficient, sin_coefficient),
                        residual_rms / denominator,
                        residual_peak / denominator,
                        step_peak / denominator,
                        dc / denominator,
                    )
                )
                clipped[channel] += int(np.count_nonzero(np.abs(values) >= 2147483647.0))
            total_frames += frames

    arrays = [np.asarray(channel_metrics, dtype=np.float64) for channel_metrics in metrics]
    if not len(arrays[0]):
        raise RuntimeError("Recording contains no complete analysis windows")

    skip_windows = int(math.ceil(args.skip_seconds * 1000.0 / args.window_ms))
    usable = [array[skip_windows:] for array in arrays]
    if min(len(array) for array in usable) < 20:
        raise RuntimeError("Recording is too short after the startup exclusion")

    amplitudes = [float(np.median(array[:, 1])) for array in usable]
    channel = int(np.argmax(amplitudes))
    selected = usable[channel]
    times = selected[:, 0]
    amplitude = selected[:, 1]
    phase = np.unwrap(selected[:, 2])
    residual_rms = selected[:, 3]
    residual_peak = selected[:, 4]
    step_peak = selected[:, 5]
    dc_ratio = np.abs(selected[:, 6])

    amplitude_median = float(np.median(amplitude))
    amplitude_deviation = np.abs(amplitude - amplitude_median) / max(amplitude_median, 1.0)
    phase_step = np.diff(phase, prepend=phase[0])
    phase_reference = float(np.median(phase_step[1:]))
    phase_error = np.abs(phase_step - phase_reference)
    phase_error[0] = 0.0

    limits = {
        "amplitude_deviation": robust_limit(amplitude_deviation, 14.0, 5.0, 0.08),
        "residual_rms_ratio": robust_limit(residual_rms, 14.0, 2.5),
        "residual_peak_ratio": robust_limit(residual_peak, 20.0, 3.0, 0.08),
        "step_peak_ratio": robust_limit(step_peak, 20.0, 2.5, 0.08),
        "phase_error_rad": robust_limit(phase_error[1:], 20.0, 4.0, 0.02),
        "dc_ratio": robust_limit(dc_ratio, 14.0, 3.0, 0.10),
    }

    reasons = []
    anomaly_rows = []
    for index, time_value in enumerate(times):
        window_reasons = []
        if amplitude_deviation[index] > limits["amplitude_deviation"]:
            window_reasons.append("amplitude")
        if residual_rms[index] > limits["residual_rms_ratio"]:
            window_reasons.append("residual_rms")
        if residual_peak[index] > limits["residual_peak_ratio"]:
            window_reasons.append("residual_peak")
        if step_peak[index] > limits["step_peak_ratio"]:
            window_reasons.append("sample_step")
        if phase_error[index] > limits["phase_error_rad"]:
            window_reasons.append("phase_jump")
        if dc_ratio[index] > limits["dc_ratio"]:
            window_reasons.append("dc")
        if window_reasons:
            reasons.extend(window_reasons)
            anomaly_rows.append(
                {
                    "time_seconds": round(float(time_value), 6),
                    "reasons": "+".join(window_reasons),
                    "amplitude": int(round(float(amplitude[index]))),
                    "amplitude_deviation": float(amplitude_deviation[index]),
                    "residual_rms_ratio": float(residual_rms[index]),
                    "residual_peak_ratio": float(residual_peak[index]),
                    "step_peak_ratio": float(step_peak[index]),
                    "phase_error_rad": float(phase_error[index]),
                    "dc_ratio": float(dc_ratio[index]),
                }
            )

    duration = total_frames / sample_rate
    snr = 20.0 * np.log10(1.0 / np.maximum(residual_rms, 1e-15))
    estimated_frequency = args.frequency + phase_reference / (2.0 * math.pi * args.window_ms / 1000.0)
    summary = {
        "file": str(args.wav.resolve()),
        "duration_seconds": duration,
        "sample_rate": sample_rate,
        "frames": total_frames,
        "analyzed_channel": channel + 1,
        "median_amplitude": amplitude_median,
        "median_amplitude_dbfs": 20.0 * math.log10(max(amplitude_median, 1.0) / 2147483647.0),
        "estimated_frequency_hz": estimated_frequency,
        "median_snr_db": float(np.median(snr)),
        "minimum_window_snr_db": float(np.min(snr)),
        "maximum_residual_peak_ratio": float(np.max(residual_peak)),
        "maximum_step_ratio": float(np.max(step_peak)),
        "maximum_phase_error_rad": float(np.max(phase_error)),
        "clipped_samples": clipped[channel],
        "analysis_windows": int(len(selected)),
        "anomaly_windows": len(anomaly_rows),
        "reason_counts": {reason: reasons.count(reason) for reason in sorted(set(reasons))},
        "limits": limits,
        "startup_seconds_excluded": args.skip_seconds,
    }

    summary_path = args.wav.with_suffix(".analysis.json")
    anomalies_path = args.wav.with_suffix(".anomalies.csv")
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    with anomalies_path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(
            output,
            fieldnames=[
                "time_seconds",
                "reasons",
                "amplitude",
                "amplitude_deviation",
                "residual_rms_ratio",
                "residual_peak_ratio",
                "step_peak_ratio",
                "phase_error_rad",
                "dc_ratio",
            ],
        )
        writer.writeheader()
        writer.writerows(anomaly_rows)

    print(json.dumps(summary, indent=2))
    print(f"summary={summary_path}")
    print(f"anomalies={anomalies_path}")
    return 0 if not anomaly_rows and not clipped[channel] else 4


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"analysis failed: {error}", file=sys.stderr)
        sys.exit(2)
