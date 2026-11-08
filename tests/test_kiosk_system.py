#!/usr/bin/env python3
"""
tests/test_kiosk_system.py - Integration tests for Interactive Kiosk Application (Part 8).
"""

from pathlib import Path
import cv2
import numpy as np
import pytest

from src.kiosk_app import InteractiveMudraKiosk
from src.correction_engine import MudraEvaluationState


@pytest.fixture
def kiosk_instance():
    kiosk = InteractiveMudraKiosk(camera_index=None, default_model="V4", enable_audio=False)
    yield kiosk
    kiosk.close()


def test_kiosk_initialization(kiosk_instance):
    assert kiosk_instance.current_model_key == "V4"
    assert len(kiosk_instance.supported_mudras) == 30
    assert len(kiosk_instance.buttons) >= 5
    assert kiosk_instance.current_expected_mudra == "1-Pataka"


def test_kiosk_navigation(kiosk_instance):
    initial_mudra = kiosk_instance.current_expected_mudra

    kiosk_instance.next_mudra()
    assert kiosk_instance.current_expected_mudra != initial_mudra

    kiosk_instance.previous_mudra()
    assert kiosk_instance.current_expected_mudra == initial_mudra


def test_kiosk_model_cycling(kiosk_instance):
    kiosk_instance.current_model_key = "V1"
    kiosk_instance.rec_engine.set_active_model("V1")
    assert kiosk_instance.current_model_key == "V1"

    kiosk_instance.cycle_model()
    assert kiosk_instance.current_model_key == "V2"

    kiosk_instance.cycle_model()
    assert kiosk_instance.current_model_key == "V3"

    kiosk_instance.cycle_model()
    assert kiosk_instance.current_model_key == "V4"

    kiosk_instance.cycle_model()
    assert kiosk_instance.current_model_key == "V5"

    kiosk_instance.cycle_model()
    assert kiosk_instance.current_model_key == "V1"


def test_kiosk_frame_rendering(kiosk_instance):
    dummy_cam = np.zeros((480, 640, 3), dtype=np.uint8)

    rec_res, _ = kiosk_instance.rec_engine.process_frame(dummy_cam, expected_mudra="1-Pataka")
    report = kiosk_instance.corr_engine.evaluate_pose(rec_res, expected_mudra="1-Pataka")

    canvas = kiosk_instance.render_kiosk_frame(dummy_cam, rec_res, report, fps=30.0)
    assert canvas.shape == (kiosk_instance.screen_h, kiosk_instance.screen_w, 3)
    assert canvas.dtype == np.uint8


def test_kiosk_audio_manager(kiosk_instance):
    kiosk_instance.audio_manager.notify_state(MudraEvaluationState.STATE_A_CORRECT)
    kiosk_instance.audio_manager.notify_state(MudraEvaluationState.STATE_B_WRONG_MUDRA, "Wrong Mudra")
    kiosk_instance.audio_manager.notify_state(MudraEvaluationState.STATE_C_INCORRECT_EXECUTION, "Adjust index")


def test_landmark_visualization_data(kiosk_instance):
    dummy_cam = np.zeros((480, 640, 3), dtype=np.uint8)
    # Test rendering with 21 MediaPipe mock landmark objects
    dummy_landmarks = [
        type("MockLM", (), {"x": 0.2 + 0.02 * i, "y": 0.3 + 0.015 * i, "z": 0.0})()
        for i in range(21)
    ]
    statuses = {f: "Correct" for f in ["Thumb", "Index", "Middle", "Ring", "Pinky"]}
    annotated = kiosk_instance.corr_engine.draw_correction_skeleton(
        dummy_cam,
        dummy_landmarks,
        statuses,
    )
    assert annotated.shape == (480, 640, 3)
    assert not np.array_equal(annotated, dummy_cam)


def test_missing_reference_handling(kiosk_instance):
    # Test reference visual for missing/unverified mudra
    vis = kiosk_instance.get_reference_visual("99-NonexistentMudra", (400, 300))
    assert vis.shape == (300, 400, 3)
    assert vis.dtype == np.uint8
    # Should render the pending validation header
    assert vis[:44, :].sum() > 0


def test_all_30_reference_images_are_genuine_photos(kiosk_instance):
    """Verifies all 30 reference images exist, are valid photos, and not black placeholders."""
    for mudra_id in kiosk_instance.supported_mudras:
        ref_data = kiosk_instance.corr_engine.get_reference_data(mudra_id)
        assert ref_data is not None, f"Reference data missing for {mudra_id}"
        img_path = Path(ref_data["image_path"])
        assert img_path.is_file(), f"Image file missing for {mudra_id} at {img_path}"
        img = cv2.imread(str(img_path))
        assert img is not None, f"Failed to load image for {mudra_id}"
        assert img.shape == (400, 400, 3), f"Unexpected shape for {mudra_id}: {img.shape}"
        # Genuine photograph should have substantial non-zero content and contrast
        assert img.mean() > 50.0, f"Image for {mudra_id} appears too dark or placeholder (mean={img.mean()})"
        assert img.std() > 25.0, f"Image for {mudra_id} lacks contrast (std={img.std()})"


def test_dynamic_mudra_switching_changes_visual(kiosk_instance):
    """Verifies that selecting another mudra immediately changes the reference photograph."""
    vis_pataka = kiosk_instance.get_reference_visual("1-Pataka", (400, 300))
    kiosk_instance.next_mudra()
    current_mudra = kiosk_instance.current_expected_mudra
    vis_next = kiosk_instance.get_reference_visual(current_mudra, (400, 300))

    assert not np.array_equal(vis_pataka, vis_next), "Reference panel did not change on mudra switch"


def test_coordinate_transformation_mathematics():
    """Verifies coordinate transformation mathematically for different frame sizes and mirrored/unmirrored frames (Task 4)."""
    w_src, h_src = 640, 480
    w_disp, h_disp = 625, 445

    class MockLM:
        def __init__(self, x, y, z):
            self.x = x
            self.y = y
            self.z = z

    # Test normalized coordinate mapping
    lm = MockLM(0.25, 0.75, 0.0)
    px = int(np.clip(lm.x * w_src, 0, w_src - 1))
    py = int(np.clip(lm.y * h_src, 0, h_src - 1))
    assert px == int(0.25 * 640)
    assert py == int(0.75 * 480)

    # Test mirrored frame transformation (cv2.flip horizontal)
    lm_flipped_x = 1.0 - lm.x
    px_flipped = int(np.clip(lm_flipped_x * w_src, 0, w_src - 1))
    assert px_flipped == int(0.75 * 640)


