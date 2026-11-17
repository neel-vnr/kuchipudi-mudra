#!/usr/bin/env python3
"""
src/recognition_engine.py - Unified Multi-Model Recognition & Comparison Engine (Parts 3 & 4).

Features:
- Encapsulates MediaPipe HandLandmarker Tasks API (in IMAGE mode).
- Supports dynamic switching between all 5 model generations (V1, V2, V3, V4, V5).
- Automatically routes the correct feature representation:
    * V1 (Baseline SVM): 63 raw normalized coordinates
    * V2 (Augmented SVM): 63 raw normalized coordinates
    * V3 (Cascade SVM): 63 raw normalized coordinates
    * V4 (Geometric SVM): 98 features (63 coords + 15 angles + 20 distances)
    * V5 (MLP Neural Net): 98 features (63 coords + 15 angles + 20 distances)
- Computes palm normal orientation and 3D direction vectors.
- Applies temporal smoothing (rolling majority vote) and confidence gating.
- Compares predicted mudra against the user-selected expected mudra.
"""

from __future__ import annotations

from collections import Counter, deque
import logging
from pathlib import Path
import sys
import time
from typing import Any, Deque, Dict, List, Optional, Tuple, Union

import cv2
import joblib
import numpy as np

src_dir = Path(__file__).resolve().parent
if str(src_dir) not in sys.path:
    sys.path.insert(0, str(src_dir))

from audit_dataset import MediaPipeHandDetector
from features import (
    draw_hand_landmarks_on_image,
    extract_v4_features,
    normalize_landmarks,
)

logger = logging.getLogger("RecognitionEngine")


def normalize_probabilities(probs: Union[np.ndarray, List[float]]) -> np.ndarray:
    """Normalizes an array or list of probability scores so they sum strictly to 1.0."""
    arr = np.asarray(probs, dtype=np.float64)
    arr = np.clip(arr, 0.0, None)
    total = float(np.sum(arr))
    if total > 0.0:
        return arr / total
    n = len(arr)
    return np.ones(n, dtype=np.float64) / max(1, n)


class PredictionItem(dict):
    """Represents a single prediction with class name and probability score.

    Supports unpacking (cls, prob = item), indexing (item[0], item[1]),
    dict access (item['class'], item['probability']), and attributes (.class_name, .probability).
    """

    def __init__(self, class_name: str, probability: float) -> None:
        prob = float(probability)
        super().__init__({
            "class": class_name,
            "class_name": class_name,
            "mudra": class_name,
            "probability": prob,
            "confidence": prob,
            "score": prob,
        })
        self._class_name = class_name
        self._probability = prob

    def __getitem__(self, key: Any) -> Any:
        if key == 0:
            return self._class_name
        elif key == 1:
            return self._probability
        return super().__getitem__(key)

    def __iter__(self):
        yield self._class_name
        yield self._probability

    def __len__(self) -> int:
        return 2

    @property
    def class_name(self) -> str:
        return self._class_name

    @property
    def mudra(self) -> str:
        return self._class_name

    @property
    def probability(self) -> float:
        return self._probability

    @property
    def confidence(self) -> float:
        return self._probability

    @property
    def score(self) -> float:
        return self._probability

    def to_dict(self) -> Dict[str, Any]:
        return {
            "class": self._class_name,
            "mudra": self._class_name,
            "probability": round(self._probability, 4),
            "score": round(self._probability, 4),
        }


# Alias for compatibility
PredictionEntry = PredictionItem


class MudraRecognitionResult:
    """Encapsulates the complete result of a recognition cycle."""

    def __init__(
        self,
        hand_detected: bool,
        predicted_mudra: str = "",
        confidence: float = 0.0,
        raw_confidence: float = 0.0,
        top1_prediction: str = "",
        top1_confidence: float = 0.0,
        top3_predictions: Optional[List[PredictionItem]] = None,
        confidence_threshold: float = 0.35,
        is_stable: bool = False,
        landmarks_21_3: Optional[np.ndarray] = None,
        normalized_63: Optional[np.ndarray] = None,
        features_98: Optional[np.ndarray] = None,
        palm_orientation: Optional[Dict[str, Any]] = None,
        active_model_name: str = "V1",
        expected_mudra: str = "",
        is_expected_match: bool = False,
        smoothed: bool = True,
        hand_landmarks: Optional[Any] = None,
        handedness: Optional[str] = None,
        # Backward-compatibility alias parameters
        top1_mudra: Optional[str] = None,
        prediction_stable: Optional[bool] = None,
    ) -> None:
        self.hand_detected = hand_detected
        self.predicted_mudra = predicted_mudra
        self.confidence = float(confidence)
        self.raw_confidence = float(raw_confidence)
        self.top1_prediction = top1_prediction or (top1_mudra if top1_mudra is not None else predicted_mudra)
        self.top1_confidence = float(top1_confidence) if top1_confidence else float(confidence)
        self.top3_predictions = top3_predictions if top3_predictions is not None else []
        self.confidence_threshold = float(confidence_threshold)
        self.is_stable = is_stable if prediction_stable is None else prediction_stable
        self.prediction_stable = self.is_stable
        self.landmarks_21_3 = landmarks_21_3
        self.normalized_63 = normalized_63
        self.features_98 = features_98
        self.palm_orientation = palm_orientation or {}
        self.active_model_name = active_model_name
        self.expected_mudra = expected_mudra
        self.is_expected_match = is_expected_match
        self.smoothed = smoothed
        self.hand_landmarks = hand_landmarks
        self.handedness = handedness or ""

    @property
    def top1_mudra(self) -> str:
        return self.top1_prediction

    @top1_mudra.setter
    def top1_mudra(self, val: str) -> None:
        self.top1_prediction = val

    def to_dict(self) -> Dict[str, Any]:
        return {
            "hand_detected": self.hand_detected,
            "predicted_mudra": self.predicted_mudra,
            "confidence": round(self.confidence, 4),
            "raw_confidence": round(self.raw_confidence, 4),
            "top1_prediction": self.top1_prediction,
            "top1_mudra": self.top1_prediction,
            "top1_confidence": round(self.top1_confidence, 4),
            "top3_predictions": [
                p.to_dict() if hasattr(p, "to_dict") else {"class": p[0], "probability": round(p[1], 4)}
                for p in self.top3_predictions
            ],
            "confidence_threshold": round(self.confidence_threshold, 4),
            "is_stable": self.is_stable,
            "prediction_stable": self.is_stable,
            "active_model_name": self.active_model_name,
            "expected_mudra": self.expected_mudra,
            "is_expected_match": self.is_expected_match,
            "palm_orientation": self.palm_orientation,
            "handedness": self.handedness,
        }


class MudraRecognitionEngine:
    """Unified Recognition Engine capable of multi-model inference and pose comparison."""

    AVAILABLE_MODELS: Dict[str, Dict[str, Any]] = {
        "AUTO": {
            "name": "AUTO Intelligent Routing (V1 Primary / V4 Fallback)",
            "path": "models/final_svm_model.joblib",
            "fallback_path": "models/v4_svm_model.joblib",
            "feature_config": "control",
            "fallback_feature_config": "angles_distances",
            "feature_dim": 63,
        },
        "V1": {
            "name": "V1 Baseline SVM",
            "path": "models/final_svm_model.joblib",
            "feature_config": "control",
            "feature_dim": 63,
        },
        "V2": {
            "name": "V2 Augmented Balanced SVM",
            "path": "models/augmented_svm_balanced.joblib",
            "feature_config": "control",
            "feature_dim": 63,
        },
        "V3": {
            "name": "V3 Cascade Unweighted SVM",
            "path": "models/v3_svm_unweighted.joblib",
            "feature_config": "control",
            "feature_dim": 63,
        },
        "V4": {
            "name": "V4 Geometric SVM",
            "path": "models/v4_svm_model.joblib",
            "feature_config": "angles_distances",
            "feature_dim": 98,
        },
        "V5": {
            "name": "V5 Selected MLP Neural Network",
            "path": "models/v5_experiment/v5_selected_mlp_model.joblib",
            "feature_config": "angles_distances",
            "feature_dim": 98,
        },
    }

    def __init__(
        self,
        default_model: str = "V1",
        min_detection_confidence: float = 0.5,
        smoothing_window: int = 7,
        confidence_threshold: float = 0.35,
        hand_landmarker_path: Path = Path("models/hand_landmarker.task"),
    ) -> None:
        self.min_detection_confidence = min_detection_confidence
        self.confidence_threshold = confidence_threshold
        self.hand_landmarker_path = hand_landmarker_path
        self.active_model_key = default_model if default_model in self.AVAILABLE_MODELS else "V1"

        # Initialize detector
        self.detector = MediaPipeHandDetector(
            model_path=self.hand_landmarker_path,
            num_hands=1,
            min_hand_detection_confidence=min_detection_confidence,
        )

        # Cache of loaded models (Warm-loaded Model Registry)
        self._loaded_models: Dict[str, Any] = {}
        for key in self.AVAILABLE_MODELS.keys():
            if key == "AUTO":
                continue
            try:
                self._load_model(key)
            except Exception as e:
                logger.warning(f"Could not warm-load model {key}: {e}")

        # Load active model with AUTO resolution
        eff_key = self.get_effective_model_key()
        self._load_model(eff_key)

        # Temporal smoother
        self.window_size = smoothing_window
        self.history: Deque[Tuple[str, float]] = deque(maxlen=smoothing_window)

    def get_effective_model_key(self) -> str:
        """Resolves AUTO to V1 primary or V4 fallback by attempting actual loading and deserialization."""
        if self.active_model_key == "AUTO":
            try:
                self._load_model("V1")
                logger.info("AUTO successfully selected primary model V1 (63 features).")
                return "V1"
            except Exception as e1:
                logger.warning(f"AUTO: V1 primary model failed to load/deserialize ({e1}). Attempting fallback to V4.")
                try:
                    self._load_model("V4")
                    logger.info("AUTO successfully fell back to model V4 (98 features).")
                    return "V4"
                except Exception as e2:
                    raise RuntimeError(f"AUTO initialization failed: Both V1 primary ({e1}) and V4 fallback ({e2}) failed to load.")
        return self.active_model_key

    def _load_model(self, model_key: str) -> Any:
        if model_key not in self.AVAILABLE_MODELS:
            raise ValueError(f"Unknown model key: {model_key}. Available: {list(self.AVAILABLE_MODELS.keys())}")

        if model_key not in self._loaded_models:
            cfg = self.AVAILABLE_MODELS[model_key]
            m_path = Path(cfg["path"])
            if not m_path.is_file():
                # Fallback to alternative paths if needed
                alt_path = Path("models/final_svm_model.joblib")
                logger.warning(f"Model file {m_path} not found. Falling back to {alt_path}")
                m_path = alt_path
            logger.info(f"Loading {cfg['name']} from {m_path}...")
            self._loaded_models[model_key] = joblib.load(m_path)

        return self._loaded_models[model_key]

    def set_active_model(self, model_key: str) -> None:
        """Dynamically switches the active classification model."""
        if model_key not in self.AVAILABLE_MODELS:
            raise ValueError(f"Unknown model key: {model_key}")
        if model_key != self.active_model_key:
            self._load_model(model_key)
            self.active_model_key = model_key
            self.clear_smoothing()
            logger.info(f"Switched active model to: {self.AVAILABLE_MODELS[model_key]['name']}")

    def clear_smoothing(self) -> None:
        """Clears the temporal smoothing buffer."""
        self.history.clear()

    @staticmethod
    def compute_palm_orientation(pts_21_3: np.ndarray) -> Dict[str, Any]:
        """Calculates 3D palm normal vector and wrist-to-middle direction."""
        wrist = pts_21_3[0]
        index_mcp = pts_21_3[5]
        pinky_mcp = pts_21_3[17]
        middle_mcp = pts_21_3[9]

        v1 = index_mcp - wrist
        v2 = pinky_mcp - wrist

        # Normal vector perpendicular to palm plane
        normal = np.cross(v1, v2)
        norm_mag = float(np.linalg.norm(normal))
        if norm_mag > 1e-6:
            normal = normal / norm_mag
        else:
            normal = np.array([0.0, 0.0, 1.0], dtype=np.float32)

        # Direction vector from wrist to middle finger base
        hand_dir = middle_mcp - wrist
        dir_mag = float(np.linalg.norm(hand_dir))
        if dir_mag > 1e-6:
            hand_dir = hand_dir / dir_mag
        else:
            hand_dir = np.array([0.0, -1.0, 0.0], dtype=np.float32)

        # Classify primary facing direction
        # In MediaPipe camera coords: +X is right, +Y is down, +Z is towards camera / depth
        z_facing = "Facing Camera" if normal[2] < 0 else "Facing Away / Rear"

        return {
            "palm_normal": [round(float(x), 4) for x in normal],
            "hand_direction": [round(float(x), 4) for x in hand_dir],
            "primary_orientation": z_facing,
        }

    def process_frame(
        self,
        bgr_frame: np.ndarray,
        expected_mudra: str = "",
    ) -> Tuple[MudraRecognitionResult, np.ndarray]:
        """Processes a single BGR camera frame.

        Returns:
            Tuple of (MudraRecognitionResult, annotated_display_frame)
        """
        import time as _time
        start = _time.perf_counter()
        display_frame = bgr_frame.copy()
        detected, result, det_err = self.detector.detect(bgr_frame)

        if not detected or result is None or not result.hand_landmarks:
            self.clear_smoothing()
            return MudraRecognitionResult(
                hand_detected=False,
                active_model_name=self.AVAILABLE_MODELS[self.active_model_key]["name"],
                expected_mudra=expected_mudra,
                confidence_threshold=self.confidence_threshold,
                prediction_stable=True,
            ), display_frame

        first_hand = result.hand_landmarks[0]
        raw_pts = np.array([[lm.x, lm.y, lm.z] for lm in first_hand], dtype=np.float32)

        # Normalize landmarks: translation (wrist=0) + max_distance scale
        norm_pts = normalize_landmarks(raw_pts, scale_mode="max_distance")
        norm_63 = norm_pts.flatten()

        # Extract features according to active model specification (with AUTO fallback support)
        effective_key = self.get_effective_model_key()
        active_cfg = self.AVAILABLE_MODELS[self.active_model_key]
        eff_cfg = self.AVAILABLE_MODELS[effective_key]
        feature_cfg_name = eff_cfg["feature_config"]
        model = self._load_model(effective_key)

        def _get_pred_for_pts(p_21_3: np.ndarray) -> Tuple[str, float, np.ndarray]:
            if feature_cfg_name == "control":
                vec = p_21_3.flatten().reshape(1, -1)
            else:
                f_98 = extract_v4_features(p_21_3, feature_config="angles_distances")
                vec = f_98.reshape(1, -1)
            pred = str(model.predict(vec)[0])
            raw_probs = model.predict_proba(vec)[0]
            probs = normalize_probabilities(raw_probs)
            conf = float(np.max(probs))
            return pred, conf, probs

        # Test both direct and canonicalized (left-to-right flipped) coordinates for dual-handedness
        norm_pts_flipped = norm_pts.copy()
        norm_pts_flipped[:, 0] = -norm_pts_flipped[:, 0]

        pred_direct, conf_direct, probs_direct = _get_pred_for_pts(norm_pts)
        pred_flipped, conf_flipped, probs_flipped = _get_pred_for_pts(norm_pts_flipped)

        handedness_label = ""
        if getattr(result, "handedness", None) and len(result.handedness) > 0:
            try:
                handedness_label = result.handedness[0][0].category_name
            except Exception:
                handedness_label = ""

        # Select highest-confidence canonical orientation (handles both left and right hands)
        if conf_flipped > conf_direct + 0.05:
            raw_pred, raw_conf, chosen_probs = pred_flipped, conf_flipped, probs_flipped
            chosen_pts = norm_pts_flipped
            if not handedness_label:
                handedness_label = "Left"
        else:
            raw_pred, raw_conf, chosen_probs = pred_direct, conf_direct, probs_direct
            chosen_pts = norm_pts
            if not handedness_label:
                handedness_label = "Right"

        norm_63 = chosen_pts.flatten()
        feat_98 = extract_v4_features(chosen_pts, feature_config="angles_distances")

        # Temporal smoothing
        self.history.append((raw_pred, raw_conf))
        classes = [p for p, _ in self.history]
        counts = Counter(classes)
        majority_pred, count = counts.most_common(1)[0]
        majority_confs = [c for p, c in self.history if p == majority_pred]
        smoothed_conf = float(np.mean(majority_confs))

        # Apply confidence gating
        final_pred = majority_pred if smoothed_conf >= self.confidence_threshold else "Uncertain"

        # Compute palm orientation
        palm_info = self.compute_palm_orientation(norm_pts)

        # Expected mudra comparison
        is_match = False
        if expected_mudra and final_pred != "Uncertain":
            # Direct match or prefix matching (e.g., '1-Pataka' matches 'Pataka')
            is_match = (
                final_pred.lower() == expected_mudra.lower()
                or final_pred.split("-")[-1].lower() == expected_mudra.split("-")[-1].lower()
            )

        # Build top-3 predictions from model class names and normalized probabilities
        top3_predictions: List[PredictionItem] = []
        try:
            classes_arr = getattr(model, "classes_", None)
            if classes_arr is None and hasattr(model, "named_steps"):
                classes_arr = getattr(model.named_steps.get("classifier", None), "classes_", None)
            if classes_arr is None:
                classes_arr = [f"Class_{i}" for i in range(len(chosen_probs))]

            top_idx = np.argsort(-chosen_probs)[:3]
            for idx in top_idx:
                cls_name = str(classes_arr[idx])
                prob = float(chosen_probs[idx])
                top3_predictions.append(PredictionItem(cls_name, prob))
        except Exception:
            if final_pred and final_pred != "Uncertain":
                top3_predictions.append(PredictionItem(final_pred, smoothed_conf))

        # Determine prediction stability after smoothing
        prediction_stable = bool(
            final_pred != "Uncertain"
            and len(self.history) >= min(3, self.window_size)
            and (count / len(self.history)) >= 0.6
            and self.history[-1][0] == majority_pred
        )

        end_tok = _time.perf_counter()
        latency_ms = (end_tok - start) * 1000

        # Overlay 21-joint skeleton
        display_frame = draw_hand_landmarks_on_image(
            display_frame,
            first_hand,
            draw_connections=True,
            draw_points=True,
            connection_color=(255, 215, 0),  # Cyan
            point_color=(0, 100, 255),       # Vibrant orange
            point_radius=4,
            line_thickness=2,
        )

        top1_p = top3_predictions[0].class_name if top3_predictions else final_pred
        top1_c = float(top3_predictions[0].probability) if top3_predictions else smoothed_conf

        res = MudraRecognitionResult(
            hand_detected=True,
            predicted_mudra=final_pred,
            confidence=smoothed_conf,
            raw_confidence=raw_conf,
            top1_prediction=top1_p,
            top1_confidence=top1_c,
            top3_predictions=top3_predictions,
            confidence_threshold=self.confidence_threshold,
            is_stable=prediction_stable,
            landmarks_21_3=norm_pts,
            normalized_63=norm_63,
            features_98=feat_98,
            palm_orientation=palm_info,
            active_model_name=f"{active_cfg['name']} -> {eff_cfg['name']}" if self.active_model_key == "AUTO" else active_cfg["name"],
            expected_mudra=expected_mudra,
            is_expected_match=is_match,
            smoothed=True,
            hand_landmarks=first_hand,
            handedness=handedness_label,
        )

        return res, display_frame

    def close(self) -> None:
        """Closes detector resources."""
        if hasattr(self, "detector") and self.detector is not None:
            self.detector.close()
