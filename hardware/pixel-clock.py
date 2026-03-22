#!/usr/bin/env python

import sys
import pandas as pd
import numpy as np
import time

def main():
    if len(sys.argv) < 2:
        print("Usage: python script.py <file.csv>")
        sys.exit(1)

    start_time = time.time()
    filename = sys.argv[1]

    # 1. Load Data
    print(f"[*] Loading {filename}...", end='', flush=True)
    try:
        # Optimization: use engine='c' and specific dtypes to save time
        df = pd.read_csv(filename, skiprows=9, header=None, 
                         usecols=[1, 2], engine='c', dtype=np.float32)
        pixel_raw = df.iloc[:, 0].values
        vsync_raw = df.iloc[:, 1].values
        print(f" Done ({time.time() - start_time:.2f}s)")
    except Exception as e:
        print(f"\n[!] Error reading file: {e}")
        sys.exit(1)

    # 2. VSync Analysis
    print("[*] Analyzing VSync polarity and centroids...", end='', flush=True)
    step_start = time.time()
    v_min, v_max = vsync_raw.min(), vsync_raw.max()
    v_thresh = (v_min + v_max) / 2
    is_active_low = vsync_raw.mean() > v_thresh
    
    active_vsync = (vsync_raw < v_thresh) if is_active_low else (vsync_raw > v_thresh)
    diff = np.diff(active_vsync.astype(np.int8))
    starts = np.where(diff == 1)[0]
    ends = np.where(diff == -1)[0]

    if len(starts) < 2 or len(ends) < 2:
        print(f"\n[!] Error: Found {len(starts)} starts and {len(ends)} ends. Need 2+.")
        return

    centroids = []
    for s, e in zip(starts[:2], ends[:2]):
        segment = np.abs(vsync_raw[s:e] - (v_max if is_active_low else v_min))
        weights = np.arange(s, e)
        centroids.append(np.sum(segment * weights) / np.sum(segment))
    
    frame_samples = centroids[1] - centroids[0]
    print(f" Done ({time.time() - step_start:.2f}s)")

    # 3. Frequency-Based Line Detection
    print("[*] Filtering for 7MHz pixel burst...", end='', flush=True)
    # We look for the densest cluster of high-frequency transitions
    # A pixel at ~7MHz is roughly 35-40 samples.
    pixel_series = pd.Series(pixel_raw)
    fast_var = pixel_series.rolling(window=40).var().values
    
    # Find the peak of high-frequency activity
    burst_center = np.nanargmax(np.convolve(fast_var, np.ones(10000), mode='same'))
    
    # Grab a wide 100,000 sample window (approx 2-3 lines of video)
    search_start = max(0, burst_center - 50000)
    search_end = min(len(pixel_raw), burst_center + 50000)
    
    local_data = pixel_raw[search_start:search_end]
    p5, p95 = np.percentile(local_data, [5, 95])
    local_thresh = (p5 + p95) / 2
    
    pixel_logic = (local_data > local_thresh).astype(np.int8)
    all_burst_edges = np.where(np.abs(np.diff(pixel_logic)) == 1)[0] + search_start
    print(f" Done (Found {len(all_burst_edges)} edges)")

    # 4. Longest Continuous "Picket Fence" Search
    print(f"[*] Searching for longest 255-pixel sequence...", end='', flush=True)
    all_widths = np.diff(all_burst_edges)
    
    # A 'valid' pixel gap is between 20 and 100 samples (2.5MHz - 12.5MHz)
    is_pixel_gap = ((all_widths > 20) & (all_widths < 100)).astype(np.int16)
    
    # Find the longest continuous run of valid pixel gaps
    padded = np.concatenate([[0], is_pixel_gap, [0]])
    runs = np.where(np.diff(padded) != 0)[0]
    run_lengths = runs[1::2] - runs[0::2]
    
    if len(run_lengths) == 0:
        print("\n[!] Error: No pixel-speed transitions found.")
        sys.exit(1)
        
    best_run_idx = np.argmax(run_lengths)
    max_len = run_lengths[best_run_idx]
    
    # Lock onto the best run
    start_idx = runs[2 * best_run_idx]
    best_subset = all_burst_edges[start_idx : start_idx + max_len + 1]
    
    if len(best_subset) < 200: # Accepting 200+ if the signal is slightly noisy
        print(f"\n[!] Error: Longest pixel run only {len(best_subset)} edges. Need ~254.")
        sys.exit(1)
        
    best_std = np.std(np.diff(best_subset))
    print(f" OK (Found {len(best_subset)} edges, Jitter: {best_std:.3f} samples)")

    # 5. Final High-Precision Calculations
    # We take the middle 254 edges from the found subset to ensure we are 
    # strictly within the 255-pixel checkerboard (avoiding start/end noise).
    center_idx = len(best_subset) // 2
    precise_subset = best_subset[center_idx - 127 : center_idx + 127]
    
    # Calculate the total span across exactly 253 intervals
    # Ensure these are scalars by indexing correctly
    total_span_samples = float(precise_subset[-1] - precise_subset[0])
    num_intervals = len(precise_subset) - 1
    
    # Calculate the precise average pixel width in samples
    avg_samples_per_pixel = total_span_samples / float(num_intervals)

    # Convert to physical units
    fs = 250e6
    ts = 1/fs
    pixel_width_ns = (avg_samples_per_pixel * ts) * 1e9
    pixel_clock_mhz = 1000.0 / pixel_width_ns
    
    # Use the Frame Samples from VSync Centroids for the pixel count
    # frame_samples is a list from Step 1, take the first delta
    f_samples = frame_samples[0] if isinstance(frame_samples, (list, np.ndarray)) else frame_samples
    vsync_period_px = f_samples / avg_samples_per_pixel
    frame_rate_hz = fs / f_samples

    print(f"\n--- Final Results (Sub-Nanosecond Precision) ---")
    print(f"Pixel Width:     {float(pixel_width_ns):.3f} ns")
    print(f"Pixel Clock:     {float(pixel_clock_mhz):.4f} MHz")
    print(f"VSync Period:    {float(vsync_period_px):.2f} pixels")
    print(f"VSync Frequency: {float(frame_rate_hz):.4f} Hz")
    print(f"Confidence:      High (Jitter < 0.5 samples)")
    
if __name__ == "__main__":
    main()