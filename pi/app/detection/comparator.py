"""
SpiderCam Pi – Frame Comparator

Walks a completed inspection pass directory, loads the matching frame from the
baseline, and computes the temperature difference per coordinate.

Claude Code instructions:

    def compare_pass(current_dir: str, baseline_dir: str) -> list[LeakAlert]:
        results = []
        for filename in os.listdir(current_dir):               # e.g. "120_45.npy"
            x, y = parse_coord(filename)
            current_frame  = load_frame(current_dir,  x, y)
            baseline_frame = load_frame(baseline_dir, x, y)

            if baseline_frame is None:
                continue   # First pass ever — nothing to compare against yet

            current_smooth  = noise_filter.apply(current_frame)
            baseline_smooth = noise_filter.apply(baseline_frame)

            diff = np.abs(current_smooth - baseline_smooth)
            alerts = leak_detector.check(diff, x, y)
            results.extend(alerts)

        return results

    def load_frame(directory, x, y) -> np.ndarray | None:
        path = os.path.join(directory, f"{x}_{y}.npy")
        return np.load(path) if os.path.exists(path) else None

    def parse_coord(filename: str) -> tuple[int, int]:
        stem = filename.replace(".npy", "")
        x, y = stem.split("_")
        return int(x), int(y)
"""

# import os
# import numpy as np
# from app.detection import noise_filter, leak_detector
