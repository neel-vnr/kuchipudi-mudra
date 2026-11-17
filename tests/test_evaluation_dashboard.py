#!/usr/bin/env python3
"""
tests/test_evaluation_dashboard.py - Comprehensive automated tests for:
- Model evaluation metric loading from actual artifacts
- Verification of exact dashboard metric values
- Missing or corrupted evaluation report handling
- Top-three prediction ordering and class probability mapping
- Probability normalization
- Live frame latency and throughput performance measurements
- Dashboard open/close controls and UI rendering integration
"""

import math
from pathlib import Path
import tempfile
import numpy as np
import pytest

from src.evaluation_dashboard import EvaluationDashboard, EvaluationMetrics
from src.kiosk_app import InteractiveMudraKiosk
from src.recognition_engine import (
    MudraRecognitionEngine,
    MudraRecognitionResult,
    PredictionItem,
    normalize_probabilities,
)


def test_metric_loading():
    """Verify that EvaluationDashboard loads real evaluation artifacts from disk."""
    dashboard = EvaluationDashboard(default_model="V1")
    metrics = dashboard.metrics

    assert isinstance(metrics, EvaluationMetrics)
    assert metrics.model_name in ("SVM RBF", "V1 Baseline SVM (Production)")
    assert metrics.model_version == "V1.0"
    assert metrics.accuracy > 0.8
    assert metrics.macro_f1 > 0.8
    assert metrics.total_test_images == 1050
    assert metrics.successful_detections == 963
    assert metrics.detection_failures == 87
    assert len(metrics.per_class_metrics) == 30
    assert metrics.model_size_mb > 0.5


def test_correct_dashboard_values():
    """Verify exact values loaded from reports/ and models/ match final evaluation results."""
    dashboard = EvaluationDashboard(default_model="V1")
    m = dashboard.metrics

    # Test conditional accuracy and macro/weighted metrics against reports/final_model_results.txt
    assert abs(m.accuracy - 0.9148) < 1e-3
    assert abs(m.macro_precision - 0.9139) < 1e-3
    assert abs(m.macro_recall - 0.9091) < 1e-3
    assert abs(m.macro_f1 - 0.9093) < 1e-3
    assert abs(m.weighted_f1 - 0.9146) < 1e-3

    # End-to-end coverage-aware accuracy
    assert abs(m.e2e_accuracy - 0.8390) < 1e-3
    assert m.total_test_images == 1050
    assert m.successful_detections == 963
    assert m.detection_failures == 87
    assert m.correct_predictions == 881

    # Verify per-class metrics structure and values
    classes = [r["class"] for r in m.per_class_metrics]
    assert "1-Pataka" in classes
    assert "6-Ardhachandra" in classes
    assert "24-Bhramara" in classes

    # 6-Ardhachandra achieved perfect F1=1.0000
    ardhachandra = next(r for r in m.per_class_metrics if r["class"] == "6-Ardhachandra")
    assert abs(ardhachandra["f1_score"] - 1.0000) < 1e-3

    # 24-Bhramara lowest F1=0.6275
    bhramara = next(r for r in m.per_class_metrics if r["class"] == "24-Bhramara")
    assert abs(bhramara["f1_score"] - 0.6275) < 1e-3


def test_missing_report_handling():
    """Verify that dashboard handles missing or corrupted report files gracefully."""
    dashboard = EvaluationDashboard()

    # Pass non-existent paths
    missing_meta = Path("non_existent_dir/missing_metadata.json")
    missing_csv = Path("non_existent_dir/missing_report.csv")
    missing_model = Path("non_existent_dir/missing_model.joblib")
    missing_cm = Path("non_existent_dir/missing_cm.png")

    metrics = dashboard.load_metrics(
        model_key="V1",
        metadata_path=missing_meta,
        report_csv_path=missing_csv,
        model_path=missing_model,
        cm_path=missing_cm,
    )

    # Should not crash and should provide fallback/safe defaults
    assert isinstance(metrics, EvaluationMetrics)
    assert metrics.accuracy >= 0.0
    assert metrics.macro_f1 >= 0.0

    # Test rendering with missing assets
    canvas = dashboard.render((1280, 720))
    assert isinstance(canvas, np.ndarray)
    assert canvas.shape == (720, 1280, 3)


def test_top_three_prediction_ordering():
    """Verify top-3 predictions are strictly ordered in descending probability."""
    # Test PredictionItem class behavior
    p1 = PredictionItem("1-Pataka", 0.85)
    p2 = PredictionItem("2-Tripataka", 0.10)
    p3 = PredictionItem("10-Shikara", 0.05)

    assert p1.class_name == "1-Pataka"
    assert p1.probability == 0.85
    assert p1[0] == "1-Pataka"
    assert p1[1] == 0.85
    assert p1["class"] == "1-Pataka"
    assert p1["probability"] == 0.85

    # Test unpack
    cls_name, prob_val = p1
    assert cls_name == "1-Pataka"
    assert prob_val == 0.85

    top3 = [p1, p2, p3]
    assert top3[0][1] >= top3[1][1] >= top3[2][1]

    # Test MudraRecognitionResult top3 encapsulation and to_dict()
    res = MudraRecognitionResult(
        hand_detected=True,
        predicted_mudra="1-Pataka",
        confidence=0.85,
        top1_prediction="1-Pataka",
        top1_confidence=0.85,
        top3_predictions=top3,
        confidence_threshold=0.35,
        is_stable=True,
        expected_mudra="1-Pataka",
        is_expected_match=True,
    )

    assert res.top1_prediction == "1-Pataka"
    assert res.top1_confidence == 0.85
    assert len(res.top3_predictions) == 3
    assert res.is_stable is True
    assert res.is_expected_match is True

    d = res.to_dict()
    assert d["top1_prediction"] == "1-Pataka"
    assert d["top1_confidence"] == 0.85
    assert len(d["top3_predictions"]) == 3
    assert d["confidence_threshold"] == 0.35
    assert d["is_stable"] is True
    assert d["is_expected_match"] is True


def test_probability_normalization():
    """Verify that normalize_probabilities ensures strict sum to 1.0."""
    # Unnormalized inputs
    raw = [2.0, 3.0, 5.0]
    norm = normalize_probabilities(raw)
    assert np.isclose(np.sum(norm), 1.0)
    assert np.allclose(norm, [0.2, 0.3, 0.5])

    # Floating point slight error
    raw_flt = np.array([0.40001, 0.39999, 0.20005])
    norm_flt = normalize_probabilities(raw_flt)
    assert np.isclose(np.sum(norm_flt), 1.0)

    # Edge case: all zeros
    zero_raw = [0.0, 0.0, 0.0, 0.0]
    norm_zero = normalize_probabilities(zero_raw)
    assert np.isclose(np.sum(norm_zero), 1.0)
    assert np.allclose(norm_zero, [0.25, 0.25, 0.25, 0.25])

    # Single element
    one_raw = [7.5]
    norm_one = normalize_probabilities(one_raw)
    assert np.isclose(norm_one[0], 1.0)


def test_live_latency_values():
    """Verify that kiosk live latency and throughput metrics are measured accurately."""
    kiosk = InteractiveMudraKiosk(camera_index=None, default_model="V1", enable_audio=False)
    dummy_frame = np.full((480, 640, 3), 40, dtype=np.uint8)

    rec_result, report, perf_stats = kiosk.process_single_frame(dummy_frame)

    assert "inference_latency_ms" in perf_stats
    assert "correction_latency_ms" in perf_stats
    assert "total_processing_ms" in perf_stats
    assert "camera_fps" in perf_stats
    assert "processing_throughput_fps" in perf_stats

    # Latencies must be non-negative
    assert perf_stats["inference_latency_ms"] >= 0.0
    assert perf_stats["correction_latency_ms"] >= 0.0
    assert perf_stats["total_processing_ms"] >= 0.0
    assert perf_stats["processing_throughput_fps"] > 0.0

    # Recognition result verification
    assert type(rec_result).__name__ == "MudraRecognitionResult"
    assert rec_result.confidence_threshold == 0.35
    assert rec_result.is_expected_match is False  # No hand in blank frame

    d = rec_result.to_dict()
    assert "top1_prediction" in d
    assert "top3_predictions" in d
    assert "confidence_threshold" in d
    assert "is_stable" in d

    kiosk.close()


def test_dashboard_open_close():
    """Verify dashboard open/close state toggling and frame rendering."""
    kiosk = InteractiveMudraKiosk(camera_index=None, default_model="V1", enable_audio=False)

    assert kiosk.eval_dashboard.is_open is False

    # Toggle open
    opened = kiosk.toggle_evaluation_dashboard()
    assert opened is True
    assert kiosk.eval_dashboard.is_open is True

    # Render frame while open
    dummy_frame = np.full((480, 640, 3), 40, dtype=np.uint8)
    rec_result, report, _ = kiosk.process_single_frame(dummy_frame)
    canvas_open = kiosk.render_kiosk_frame(dummy_frame, rec_result, report, fps=30.0)
    assert canvas_open.shape == (720, 1280, 3)

    # Toggle close
    closed = kiosk.toggle_evaluation_dashboard()
    assert closed is False
    assert kiosk.eval_dashboard.is_open is False

    # Render frame while closed
    canvas_closed = kiosk.render_kiosk_frame(dummy_frame, rec_result, report, fps=30.0)
    assert canvas_closed.shape == (720, 1280, 3)

    # Test open/close explicit methods
    kiosk.open_evaluation_dashboard()
    assert kiosk.eval_dashboard.is_open is True
    kiosk.close_evaluation_dashboard()
    assert kiosk.eval_dashboard.is_open is False

    kiosk.close()
