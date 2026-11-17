#!/usr/bin/env python3
"""
scripts/evaluate_auto_router.py - Stage 1.1: Resolve Evaluation Anomalies and Prepare V4 AUTO.

Performs stratified 5-fold cross-validation on the 2,181 training samples with strict model.classes_ alignment
to generate correct out-of-fold (OOF) prediction matrices for V1, V2, V3, V4, and V5.
Saves experimental results under reports/auto_experiment/.
"""

from __future__ import annotations

import argparse
import csv
import json
import logging
from pathlib import Path
import re
import sys
import time
from typing import Any, Dict, List, Tuple

import joblib
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
    precision_score,
    recall_score,
    log_loss,
)
from sklearn.model_selection import StratifiedKFold
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.svm import SVC

src_dir = Path(__file__).resolve().parent.parent / "src"
if str(src_dir) not in sys.path:
    sys.path.insert(0, str(src_dir))

from features import extract_v4_features

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger("EvaluateAutoRouter")


def natural_sort_key(s: Any) -> List[int | str]:
    val = str(s)
    return [int(text) if text.isdigit() else text.lower() for text in re.split(r"(\d+)", val)]


def load_training_data(csv_path: Path) -> Tuple[np.ndarray, np.ndarray, np.ndarray, List[str], List[str]]:
    if not csv_path.is_file():
        raise FileNotFoundError(f"Training data not found at: {csv_path}")

    df = pd.read_csv(csv_path)
    valid_df = df[df["landmark_detected"] == True].copy()
    logger.info(f"Loaded {len(valid_df)} valid training samples from {csv_path}")

    coord_cols = [c for c in valid_df.columns if c.startswith(("x", "y", "z")) and c[1:].isdigit()]
    if len(coord_cols) != 63:
        raise ValueError(f"Expected 63 coordinate columns, found {len(coord_cols)}")

    X_coords = valid_df[coord_cols].values.astype(np.float32)
    y = valid_df["class_name"].values

    X_98 = np.array([extract_v4_features(pts.reshape(21, 3), feature_config="angles_distances") for pts in X_coords], dtype=np.float32)

    classes = sorted(list(valid_df["class_name"].unique()), key=natural_sort_key)
    return X_coords, X_98, y, coord_cols, classes


def train_and_predict_fold(
    X_train_63: np.ndarray,
    X_train_98: np.ndarray,
    y_train: np.ndarray,
    X_val_63: np.ndarray,
    X_val_98: np.ndarray,
    canonical_classes: List[str],
) -> Dict[str, Tuple[np.ndarray, np.ndarray, List[str]]]:
    """Trains V1–V5 pipelines and returns (preds, probs, model_classes) for each model."""
    results = {}

    def _eval_pipe(pipe: Pipeline, X_tr: np.ndarray, X_te: np.ndarray, y_tr: np.ndarray) -> Tuple[np.ndarray, np.ndarray, List[str]]:
        pipe.fit(X_tr, y_tr)
        model_classes = list(pipe.classes_)
        raw_probs = pipe.predict_proba(X_te)
        preds = pipe.predict(X_te)

        # Align raw_probs columns from model_classes to canonical_classes
        aligned_probs = np.zeros((len(X_te), len(canonical_classes)), dtype=np.float32)
        for c_idx, c_name in enumerate(canonical_classes):
            if c_name in model_classes:
                src_idx = model_classes.index(c_name)
                aligned_probs[:, c_idx] = raw_probs[:, src_idx]

        # Diagnostic assertions
        assert np.all(np.isfinite(aligned_probs)), "Probabilities must be finite"
        assert np.all(aligned_probs >= 0.0), "Probabilities must be non-negative"
        row_sums = np.sum(aligned_probs, axis=1)
        assert np.allclose(row_sums, 1.0, atol=1e-3), f"Probability rows must sum to 1, got min={row_sums.min()}, max={row_sums.max()}"

        return preds, aligned_probs, model_classes

    # V1: Baseline SVM
    pipe_v1 = Pipeline([("scaler", StandardScaler()), ("svc", SVC(kernel="rbf", C=10.0, probability=True, random_state=42))])
    results["V1"] = _eval_pipe(pipe_v1, X_train_63, X_val_63, y_train)

    # V2: Augmented Balanced SVM
    pipe_v2 = Pipeline([("scaler", StandardScaler()), ("svc", SVC(kernel="rbf", C=10.0, class_weight="balanced", probability=True, random_state=42))])
    results["V2"] = _eval_pipe(pipe_v2, X_train_63, X_val_63, y_train)

    # V3: Unweighted SVM
    pipe_v3 = Pipeline([("scaler", StandardScaler()), ("svc", SVC(kernel="rbf", C=10.0, probability=True, random_state=42))])
    results["V3"] = _eval_pipe(pipe_v3, X_train_63, X_val_63, y_train)

    # V4: Geometric SVM
    pipe_v4 = Pipeline([("scaler", StandardScaler()), ("svc", SVC(kernel="rbf", C=15.0, probability=True, random_state=42))])
    results["V4"] = _eval_pipe(pipe_v4, X_train_98, X_val_98, y_train)

    # V5: MLP
    pipe_v5 = Pipeline([
        ("scaler", StandardScaler()),
        ("mlp", MLPClassifier(hidden_layer_sizes=(128, 64), activation="relu", solver="adam", max_iter=200, random_state=42))
    ])
    results["V5"] = _eval_pipe(pipe_v5, X_train_98, X_val_98, y_train)

    return results


def run_evaluation_pipeline(
    train_csv: Path,
    reports_dir: Path,
) -> None:
    reports_dir.mkdir(parents=True, exist_ok=True)
    X_63, X_98, y, _, classes = load_training_data(train_csv)

    skf = StratifiedKFold(n_splits=5, shuffle=True, random_state=42)

    oof_records = []
    logger.info("Running Stratified 5-Fold Cross-Validation for OOF predictions with class alignment...")
    fold_idx = 0
    for train_idx, val_idx in skf.split(X_63, y):
        fold_idx += 1
        logger.info(f"Processing fold {fold_idx}/5...")

        X_tr_63, X_te_63 = X_63[train_idx], X_63[val_idx]
        X_tr_98, X_te_98 = X_98[train_idx], X_98[val_idx]
        y_tr, y_te = y[train_idx], y[val_idx]

        fold_preds = train_and_predict_fold(X_tr_63, X_tr_98, y_tr, X_te_63, X_te_98, classes)

        for i, val_sample_idx in enumerate(val_idx):
            true_cls = y_te[i]
            record = {"sample_id": int(val_sample_idx), "true_class": true_cls, "fold": fold_idx}
            for m_key in ["V1", "V2", "V3", "V4", "V5"]:
                pred_cls, probs, _ = fold_preds[m_key]
                record[f"{m_key}_pred"] = pred_cls[i]
                for c_idx, c_name in enumerate(classes):
                    record[f"{m_key}_prob_{c_name}"] = float(probs[i, c_idx])
            oof_records.append(record)

    df_oof = pd.DataFrame(oof_records)
    oof_csv_path = reports_dir / "oof_predictions.csv"
    df_oof.to_csv(oof_csv_path, index=False)
    logger.info(f"Saved OOF predictions CSV: {oof_csv_path}")

    model_keys = ["V1", "V2", "V3", "V4", "V5"]
    eval_results = []
    y_true = df_oof["true_class"].values

    for m_key in model_keys:
        y_pred = df_oof[f"{m_key}_pred"].values
        acc = accuracy_score(y_true, y_pred)
        prec = precision_score(y_true, y_pred, average="macro", zero_division=0)
        rec = recall_score(y_true, y_pred, average="macro", zero_division=0)
        f1 = f1_score(y_true, y_pred, average="macro", zero_division=0)
        wf1 = f1_score(y_true, y_pred, average="weighted", zero_division=0)

        prob_cols = [f"{m_key}_prob_{c}" for c in classes]
        prob_matrix = df_oof[prob_cols].values
        loss = log_loss(y_true, prob_matrix, labels=classes)

        eval_results.append({
            "Strategy": f"Individual {m_key}",
            "Accuracy": round(acc, 4),
            "Macro Precision": round(prec, 4),
            "Macro Recall": round(rec, 4),
            "Macro F1": round(f1, 4),
            "Weighted F1": round(wf1, 4),
            "Log Loss": round(loss, 4),
        })

    # Simple Probability Averaging Ensemble (with properly aligned columns)
    avg_probs = np.zeros((len(df_oof), len(classes)))
    for m_key in model_keys:
        prob_cols = [f"{m_key}_prob_{c}" for c in classes]
        avg_probs += df_oof[prob_cols].values
    avg_probs /= len(model_keys)

    avg_pred_indices = np.argmax(avg_probs, axis=1)
    avg_preds = np.array([classes[i] for i in avg_pred_indices])

    eval_results.append({
        "Strategy": "Simple Prob Averaging",
        "Accuracy": round(accuracy_score(y_true, avg_preds), 4),
        "Macro Precision": round(precision_score(y_true, avg_preds, average="macro", zero_division=0), 4),
        "Macro Recall": round(recall_score(y_true, avg_preds, average="macro", zero_division=0), 4),
        "Macro F1": round(f1_score(y_true, avg_preds, average="macro", zero_division=0), 4),
        "Weighted F1": round(f1_score(y_true, avg_preds, average="weighted", zero_division=0), 4),
        "Log Loss": round(log_loss(y_true, avg_probs, labels=classes), 4),
    })

    df_strategy = pd.DataFrame(eval_results)
    strategy_csv_path = reports_dir / "auto_strategy_comparison.csv"
    df_strategy.to_csv(strategy_csv_path, index=False)
    logger.info(f"Saved AUTO strategy comparison: {strategy_csv_path}")

    print("\n" + df_strategy.to_string(index=False) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Evaluate AUTO Router with Aligned Probabilities")
    parser.add_argument("--train-csv", type=Path, default=Path("data/processed/train_landmarks.csv"))
    parser.add_argument("--reports-dir", type=Path, default=Path("reports/auto_experiment"))
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    try:
        run_evaluation_pipeline(args.train_csv, args.reports_dir)
    except Exception as e:
        logger.exception(f"Auto evaluation failed: {e}")
        sys.exit(1)
