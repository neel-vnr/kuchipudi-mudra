#!/usr/bin/env python3
"""
scripts/evaluate_production_models.py - Stage 1.2: Correct Production-Model Evaluation.

Evaluates already-trained production model artifacts (V1–V5) directly on the test split
(test_landmarks.csv) using their respective feature configurations (63 coords for V1–V3,
98 geometric features for V4–V5). Computes accurate Accuracy, Macro F1, Weighted F1, Log Loss,
and multi-class Brier Score. Performs high-precision latency profiling using time.perf_counter_ns().
"""

from __future__ import annotations

import argparse
import csv
import logging
from pathlib import Path
import re
import sys
import time

import joblib
import numpy as np
import pandas as pd
from sklearn.metrics import (
    accuracy_score,
    f1_score,
    precision_score,
    recall_score,
    log_loss,
)

src_dir = Path(__file__).resolve().parent.parent / "src"
if str(src_dir) not in sys.path:
    sys.path.insert(0, str(src_dir))

from features import extract_v4_features

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger("EvaluateProductionModels")


def natural_sort_key(s: Any) -> List[int | str]:
    val = str(s)
    return [int(text) if text.isdigit() else text.lower() for text in re.split(r"(\d+)", val)]


def multi_class_brier_score(y_true: np.ndarray, y_prob: np.ndarray, classes: List[str]) -> float:
    n_samples = len(y_true)
    n_classes = len(classes)
    class_to_idx = {c: i for i, c in enumerate(classes)}
    Y_onehot = np.zeros((n_samples, n_classes), dtype=np.float32)
    for i, t in enumerate(y_true):
        if t in class_to_idx:
            Y_onehot[i, class_to_idx[t]] = 1.0
    return float(np.mean(np.sum((y_prob - Y_onehot) ** 2, axis=1)))


def evaluate_artifacts(test_csv: Path, models_dir: Path, reports_dir: Path) -> None:
    reports_dir.mkdir(parents=True, exist_ok=True)

    if not test_csv.is_file():
        raise FileNotFoundError(f"Test CSV not found at: {test_csv}")

    df = pd.read_csv(test_csv)
    valid_df = df[df["landmark_detected"] == True].copy()
    logger.info(f"Loaded {len(valid_df)} valid test samples out of {len(df)} total")

    coord_cols = [c for c in valid_df.columns if c.startswith(("x", "y", "z")) and c[1:].isdigit()]
    X_coords = valid_df[coord_cols].values.astype(np.float32)
    y_true = valid_df["class_name"].values
    classes = sorted(list(valid_df["class_name"].unique()), key=natural_sort_key)

    X_98 = np.array([extract_v4_features(pts.reshape(21, 3), feature_config="angles_distances") for pts in X_coords], dtype=np.float32)

    model_artifacts = {
        "V1": {"path": models_dir / "final_svm_model.joblib", "feat": "63"},
        "V2": {"path": models_dir / "augmented_svm_balanced.joblib", "feat": "63"},
        "V3": {"path": models_dir / "v3_svm_unweighted.joblib", "feat": "63"},
        "V4": {"path": models_dir / "v4_svm_model.joblib", "feat": "98"},
        "V5": {"path": models_dir / "v5_experiment" / "v5_selected_mlp_model.joblib", "feat": "98"},
    }

    eval_rows = []
    latency_records = {}

    for m_key, meta in model_artifacts.items():
        m_path = meta["path"]
        if not m_path.is_file():
            logger.warning(f"Model artifact {m_key} not found at {m_path}")
            continue

        model = joblib.load(m_path)
        logger.info(f"Evaluated production artifact {m_key} from {m_path}")

        X_test = X_coords if meta["feat"] == "63" else X_98

        model_classes = list(getattr(model, "classes_", classes))
        if not model_classes or model_classes == classes:
            if hasattr(model, "named_steps"):
                for step_name, step_obj in model.named_steps.items():
                    if hasattr(step_obj, "classes_"):
                        model_classes = list(step_obj.classes_)
                        break

        # Warm-up runs
        for _ in range(10):
            _ = model.predict(X_test[:5])

        # Timed inference runs
        lat_ns = []
        for _ in range(50):
            t0 = time.perf_counter_ns()
            _ = model.predict(X_test)
            lat_ns.append(time.perf_counter_ns() - t0)

        per_sample_ms = (np.median(lat_ns) / len(X_test)) / 1e6
        p95_sample_ms = (np.percentile(lat_ns, 95) / len(X_test)) / 1e6

        latency_records[m_key] = {"median_ms": per_sample_ms, "p95_ms": p95_sample_ms}

        y_pred = model.predict(X_test)
        raw_probs = model.predict_proba(X_test)

        aligned_probs = np.zeros((len(X_test), len(classes)), dtype=np.float32)
        for c_idx, c_name in enumerate(classes):
            if c_name in model_classes:
                src_idx = model_classes.index(c_name)
                aligned_probs[:, c_idx] = raw_probs[:, src_idx]

        acc = accuracy_score(y_true, y_pred)
        prec = precision_score(y_true, y_pred, average="macro", zero_division=0)
        rec = recall_score(y_true, y_pred, average="macro", zero_division=0)
        f1 = f1_score(y_true, y_pred, average="macro", zero_division=0)
        wf1 = f1_score(y_true, y_pred, average="weighted", zero_division=0)
        loss = log_loss(y_true, aligned_probs, labels=classes)
        brier = multi_class_brier_score(y_true, aligned_probs, classes)

        eval_rows.append({
            "Model": m_key,
            "Feature Dim": meta["feat"],
            "Accuracy": round(acc, 4),
            "Macro Precision": round(prec, 4),
            "Macro Recall": round(rec, 4),
            "Macro F1": round(f1, 4),
            "Weighted F1": round(wf1, 4),
            "Log Loss": round(loss, 4),
            "Brier Score": round(brier, 4),
            "Median Latency (ms)": round(per_sample_ms, 3),
            "p95 Latency (ms)": round(p95_sample_ms, 3),
        })

    # Feature extraction benchmarking
    feat_63_ns = []
    for _ in range(100):
        t0 = time.perf_counter_ns()
        _ = X_coords[0].flatten()
        feat_63_ns.append(time.perf_counter_ns() - t0)

    feat_98_ns = []
    for _ in range(50):
        t0 = time.perf_counter_ns()
        _ = extract_v4_features(X_coords[0].reshape(21, 3), feature_config="angles_distances")
        feat_98_ns.append(time.perf_counter_ns() - t0)

    df_eval = pd.DataFrame(eval_rows)
    prod_csv_path = reports_dir / "production_model_evaluation.csv"
    df_eval.to_csv(prod_csv_path, index=False)
    logger.info(f"Saved production evaluation CSV: {prod_csv_path}")

    bench_txt_path = reports_dir / "production_latency_benchmark.txt"
    with open(bench_txt_path, "w", encoding="utf-8") as f:
        f.write("=" * 65 + "\n")
        f.write("      MUDRAGURU — PRODUCTION ARTIFACT LATENCY & ACCURACY REPORT\n")
        f.write("=" * 65 + "\n\n")
        f.write("1. PRODUCTION ARTIFACT PERFORMANCE (Independent Test Split - 963 Samples):\n")
        f.write("-" * 50 + "\n")
        f.write(df_eval.to_string(index=False) + "\n\n")
        f.write("2. FEATURE EXTRACTION PROFILING (time.perf_counter_ns):\n")
        f.write("-" * 50 + "\n")
        f.write(f"   - 63-Feature Extraction (Median): {np.median(feat_63_ns)/1e6:.3f} ms\n")
        f.write(f"   - 98-Feature Extraction (Median): {np.median(feat_98_ns)/1e6:.3f} ms\n")
        f.write("=" * 65 + "\n")

    logger.info(f"Saved production benchmark report: {bench_txt_path}")
    print("\n" + df_eval.to_string(index=False) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Evaluate Production Model Artifacts")
    parser.add_argument("--test-csv", type=Path, default=Path("data/processed/test_landmarks.csv"))
    parser.add_argument("--models-dir", type=Path, default=Path("models"))
    parser.add_argument("--reports-dir", type=Path, default=Path("reports/auto_experiment"))
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    try:
        evaluate_artifacts(args.test_csv, args.models_dir, args.reports_dir)
    except Exception as e:
        logger.exception(f"Production evaluation failed: {e}")
        sys.exit(1)
