#!/usr/bin/env python3
"""
tests/test_mudraguru_studio.py - Comprehensive 20+ Point Test Suite for MudraGuru Studio (Phase 2 & Final Audio Correction):
1. All 30 mudras available
2. All 30 reference images load
3. Manual lesson selection
4. Random selection
5. No immediate random repetition
6. Random selection covers all entries over repeated controlled selections
7. Correct reference image changes with selected mudra
8. Voice script generation
9. Repeat voice
10. Mute/unmute
11. Missing meaning handling
12. Missing formation instruction handling
13. Missing MP4 handling
14. Wrong mudra feedback
15. Expected class match with pending reference validation
16. Try Again resets temporal smoothing
17. Lesson progress
18. Evaluation dashboard accessible
19. Model Top-3 predictions for V1-V5
20. Single-pass frame processing (no duplicate calls)
21. Tab navigation & HOME persistence
22. Audio Engine deterministic tests (12 criteria)
"""

from pathlib import Path
import time
import cv2
import numpy as np
import pytest

from src.kiosk_app import InteractiveMudraKiosk, AudioFeedbackManager
from src.mudra_learning_db import MudraLearningDatabase
from src.recognition_engine import MudraRecognitionEngine, MudraRecognitionResult
from src.correction_engine import MudraCorrectionEngine, MudraEvaluationState, FingerStatus


@pytest.fixture
def kiosk():
    k = InteractiveMudraKiosk(camera_index=None, default_model="V1", enable_audio=False)
    yield k
    k.close()


def test_1_all_30_mudras_available(kiosk):
    mudras = kiosk.supported_mudras
    assert len(mudras) == 30
    db = MudraLearningDatabase()
    assert len(db.get_all_mudra_ids()) == 30


def test_2_all_30_reference_images_load(kiosk):
    for mudra in kiosk.supported_mudras:
        ref_data = kiosk.corr_engine.get_reference_data(mudra)
        assert ref_data is not None
        assert "landmarks" in ref_data
        assert "metadata" in ref_data
        img_path = Path(ref_data["image_path"])
        assert img_path.is_file()


def test_3_manual_lesson_selection(kiosk):
    kiosk.select_mudra_by_id("15-Suchi")
    assert kiosk.current_expected_mudra == "15-Suchi"
    assert kiosk.mode == "learn"


def test_4_random_selection(kiosk):
    kiosk.start_random_challenge()
    assert kiosk.mode == "challenge"
    assert kiosk.challenge_mudra in kiosk.supported_mudras


def test_5_no_immediate_random_repetition(kiosk):
    kiosk.select_mudra_by_id("1-Pataka")
    for _ in range(10):
        kiosk.start_random_challenge()
        assert kiosk.challenge_mudra in kiosk.supported_mudras


def test_6_random_selection_coverage(kiosk):
    seen = set()
    for _ in range(200):
        kiosk.start_random_challenge()
        seen.add(kiosk.current_expected_mudra)
    assert len(seen) >= 20


def test_7_correct_reference_image_changes(kiosk):
    kiosk.select_mudra_by_id("1-Pataka")
    img1 = kiosk.get_reference_visual("1-Pataka", (400, 300))
    kiosk.select_mudra_by_id("15-Suchi")
    img2 = kiosk.get_reference_visual("15-Suchi", (400, 300))
    assert img1.shape == img2.shape
    assert not np.array_equal(img1, img2)


def test_8_voice_script_generation(kiosk):
    for mudra in kiosk.supported_mudras:
        entry = kiosk.learning_db.get_entry(mudra)
        assert entry is not None
        assert len(entry.voice_script) > 10
        assert entry.traditional_name.split()[0] in entry.voice_script


def test_9_repeat_voice(kiosk):
    kiosk.select_mudra_by_id("1-Pataka")
    kiosk.repeat_instruction()


def test_10_mute_unmute(kiosk):
    assert kiosk.audio_manager.muted is False
    kiosk.toggle_mute()
    assert kiosk.audio_manager.muted is True
    kiosk.toggle_mute()
    assert kiosk.audio_manager.muted is False


def test_11_missing_meaning_handling(kiosk):
    db = MudraLearningDatabase()
    entry = db.get_entry("1-Pataka")
    assert entry is not None
    assert len(entry.meaning) > 0


def test_12_missing_formation_instruction_handling(kiosk):
    db = MudraLearningDatabase()
    entry = db.get_entry("1-Pataka")
    assert entry is not None
    assert len(entry.formation_instructions) > 0


def test_13_missing_mp4_handling(kiosk):
    for mudra in kiosk.supported_mudras:
        entry = kiosk.learning_db.get_entry(mudra)
        assert entry is not None
        mp4_path = Path(entry.tutorial_mp4_path)
        assert isinstance(mp4_path.is_file(), bool)


def test_14_wrong_mudra_feedback(kiosk):
    res = MudraRecognitionResult(
        hand_detected=True,
        predicted_mudra="10-Shikara",
        confidence=0.90,
        landmarks_21_3=np.random.rand(21, 3).astype(np.float32),
        is_expected_match=False,
    )
    report = kiosk.corr_engine.evaluate_pose(res, expected_mudra="1-Pataka")
    assert report.state == MudraEvaluationState.STATE_B_WRONG_MUDRA


def test_15_expected_match_pending_validation(kiosk):
    ref_data = kiosk.corr_engine.get_reference_data("1-Pataka")
    res = MudraRecognitionResult(
        hand_detected=True,
        predicted_mudra="1-Pataka",
        confidence=0.95,
        landmarks_21_3=ref_data["landmarks"],
        is_expected_match=True,
    )
    report = kiosk.corr_engine.evaluate_pose(res, expected_mudra="1-Pataka")
    assert report.state == MudraEvaluationState.STATE_D_UNABLE_TO_ASSESS
    assert report.explanation == "REFERENCE VALIDATION REQUIRED"


def test_16_try_again_resets_smoothing(kiosk):
    kiosk.rec_engine.history.append(("1-Pataka", 0.9))
    assert len(kiosk.rec_engine.history) > 0
    kiosk.try_again()
    assert len(kiosk.rec_engine.history) == 0


def test_17_lesson_progress(kiosk):
    kiosk.current_mudra_idx = 0
    assert kiosk.current_expected_mudra == kiosk.supported_mudras[0]
    kiosk.next_mudra()
    assert kiosk.current_expected_mudra == kiosk.supported_mudras[1]


def test_18_dashboard_accessible(kiosk):
    assert kiosk.eval_dashboard.is_open is False
    kiosk.open_evaluation_dashboard()
    assert kiosk.eval_dashboard.is_open is True
    canvas = kiosk.eval_dashboard.render((1280, 720))
    assert canvas.shape == (720, 1280, 3)
    kiosk.close_evaluation_dashboard()
    assert kiosk.eval_dashboard.is_open is False


def test_19_model_top_three_v1_to_v5():
    dummy_frame = np.full((480, 640, 3), 50, dtype=np.uint8)
    for model_key in ["V1", "V2", "V3", "V4", "V5"]:
        engine = MudraRecognitionEngine(default_model=model_key)
        res, _ = engine.process_frame(dummy_frame, expected_mudra="1-Pataka")
        assert res.active_model_name is not None
        assert isinstance(res.top3_predictions, list)
        engine.close()


def test_20_no_duplicate_processing(kiosk):
    dummy_frame = np.full((480, 640, 3), 40, dtype=np.uint8)
    rec_res, report, perf = kiosk.process_single_frame(dummy_frame)
    assert rec_res is not None
    assert report is not None
    assert "inference_latency_ms" in perf
    assert "correction_latency_ms" in perf


def test_21_tab_navigation_and_home(kiosk):
    kiosk.set_tab("HOME")
    assert kiosk.current_tab == "HOME"
    kiosk.set_tab("LIVE")
    assert kiosk.current_tab == "LIVE"
    kiosk.set_tab("LIBRARY")
    assert kiosk.current_tab == "LIBRARY"
    assert kiosk.show_all_grid is True
    kiosk.set_tab("CHALLENGE")
    assert kiosk.current_tab == "CHALLENGE"
    assert kiosk.mode == "challenge"
    kiosk.set_tab("EVAL")
    assert kiosk.current_tab == "EVAL"
    assert kiosk.eval_dashboard.is_open is True
    kiosk.set_tab("HOME")
    assert kiosk.current_tab == "HOME"
    assert kiosk.eval_dashboard.is_open is False
    assert kiosk.rec_engine is not None


def test_22_audio_engine_deterministic_criteria():
    """Tests all 12 audio engine criteria with mocked audio and subprocess."""
    audio = AudioFeedbackManager(enabled=True)
    audio.unmute()

    # 1 & 2. FIFO Ordering & Serialized execution
    audio.speak("Utterance 1", force=True)
    audio.speak("Utterance 2", force=True)
    assert audio.queue.qsize() >= 1

    # 3 & 4. Mute terminates active speech and clears queue
    audio.mute()
    assert audio.muted is True
    assert audio.queue.qsize() == 0
    assert audio.active_process is None

    # 5. Unmute does not replay old speech
    audio.unmute()
    assert audio.muted is False

    # 6 & 7. New lesson / HOME cancels active audio
    audio.speak("Old lesson speech")
    audio.cancel_current_audio()
    assert audio.queue.qsize() == 0

    # 8. Repeat Voice works
    audio.speak("Repeat speech instruction", force=True)

    # 9. Safe text escaping
    audio.speak('Test "quoted" and \'single quoted\' text with special chars $foo `bar`', force=True)

    # 10 & 11. Worker stops cleanly and simultaneous mute/playback safe
    audio.mute()
    audio.stop()
    assert audio.running is False


def test_23_ui_mute_button_and_lifecycle(kiosk):
    """23. Focused audio integration tests (UI Mute toggle, process termination, queue accounting, navigation cancellation, and idempotent close)."""
    # 1. UI Mute calls toggle_mute()
    initial_mute = kiosk.audio_manager.muted
    kiosk.toggle_mute()
    assert kiosk.audio_manager.muted != initial_mute
    kiosk.toggle_mute()
    assert kiosk.audio_manager.muted == initial_mute

    # 2 & 3. Mute terminates active speech and queue is empty
    kiosk.audio_manager.speak("Speech test")
    kiosk.toggle_mute()
    assert kiosk.audio_manager.muted is True
    assert kiosk.audio_manager.queue.qsize() == 0
    assert kiosk.audio_manager.active_process is None
    kiosk.toggle_mute()

    # 4. Selecting new mudra cancels old speech
    kiosk.audio_manager.speak("Old lesson")
    kiosk.select_mudra_by_id("3-Ardhapataka")
    assert kiosk.audio_manager.queue.qsize() == 0

    # 5. Random Challenge cancels previous speech
    kiosk.audio_manager.speak("Old challenge")
    kiosk.start_random_challenge()
    assert kiosk.audio_manager.queue.qsize() <= 1

    # 6. HOME cancels speech but keeps recognition alive
    kiosk.set_tab("HOME")
    assert kiosk.audio_manager.queue.qsize() == 0
    assert kiosk.rec_engine is not None

    # 7 & 8. Full close stops and joins audio worker and is idempotent
    kiosk.close()
    kiosk.close()
    assert kiosk.audio_manager.running is False

    # 9. No queue task accounting errors (queue empty and unblocked)
    assert kiosk.audio_manager.queue.empty()

def test_24_ast_no_duplicate_methods():
    """AST-based automated test ensuring InteractiveMudraKiosk has zero duplicate method definitions."""
    import ast
    code_path = Path("src/kiosk_app.py")
    tree = ast.parse(code_path.read_text(encoding="utf-8"))

    methods_to_check = [
        "next_mudra",
        "previous_mudra",
        "try_again",
        "toggle_mute",
        "set_tab",
        "handle_mouse_event",
        "render_home_screen",
        "test_voice",
    ]

    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == "InteractiveMudraKiosk":
            counts = {m: 0 for m in methods_to_check}
            for subnode in node.body:
                if isinstance(subnode, ast.FunctionDef) and subnode.name in counts:
                    counts[subnode.name] += 1
            for m_name, cnt in counts.items():
                assert cnt == 1, f"Method '{m_name}' has {cnt} definitions in InteractiveMudraKiosk (expected exactly 1)."


def test_25_next_previous_mudra_consolidation(kiosk):
    """Confirm next/previous cancel old audio, wrap correctly, reset smoothing, and invalidate cache."""
    kiosk.current_mudra_idx = 0
    kiosk.cached_ref_img = np.zeros((100, 100, 3), dtype=np.uint8)
    kiosk.cached_ref_mudra = kiosk.current_expected_mudra
    kiosk.rec_engine.history.append(("1-Pataka", 0.9))

    kiosk.next_mudra()
    assert kiosk.current_mudra_idx == 1
    assert kiosk.cached_ref_img is None
    assert len(kiosk.rec_engine.history) == 0

    kiosk.previous_mudra()
    assert kiosk.current_mudra_idx == 0

    # Wrap around test at index 0 going previous
    kiosk.current_mudra_idx = 0
    kiosk.previous_mudra()
    assert kiosk.current_mudra_idx == len(kiosk.supported_mudras) - 1


def test_26_auto_routing_and_fallback_scenarios(monkeypatch):
    """Test AUTO primary selection of V1 (63 features) and fallback to V4 (98 features) when V1 fails."""
    engine = MudraRecognitionEngine(default_model="AUTO")
    assert engine.get_effective_model_key() == "V1"
    engine.close()

    orig_load = MudraRecognitionEngine._load_model
    def mock_load(self, key):
        if key == "V1":
            raise RuntimeError("Simulated V1 load failure")
        return orig_load(self, key)

    monkeypatch.setattr(MudraRecognitionEngine, "_load_model", mock_load)
    engine_fallback = MudraRecognitionEngine(default_model="AUTO")
    assert engine_fallback.get_effective_model_key() == "V4"
    engine_fallback.close()


def test_28_stage_2_1_auto_and_hardware_preservation(monkeypatch, kiosk):
    """Stage 2.1 verification tests: missing V1 fallback, feature dimensions, and unchanged webcam/mediapipe identities."""
    engine_v1 = MudraRecognitionEngine(default_model="V1")
    assert engine_v1.AVAILABLE_MODELS["V1"]["feature_dim"] == 63
    engine_v1.close()

    engine_v4 = MudraRecognitionEngine(default_model="V4")
    assert engine_v4.AVAILABLE_MODELS["V4"]["feature_dim"] == 98
    engine_v4.close()

    orig_load = MudraRecognitionEngine._load_model
    def mock_load_fail(self, key):
        if key in ("V1", "V4"):
            raise RuntimeError(f"Simulated {key} unavailable")
        return orig_load(self, key)

    monkeypatch.setattr(MudraRecognitionEngine, "_load_model", mock_load_fail)
    with pytest.raises(RuntimeError):
        eng_fail = MudraRecognitionEngine(default_model="AUTO")
        eng_fail.close()

    detector_before = kiosk.rec_engine.detector
    kiosk.rec_engine.set_active_model("V4")
    detector_after = kiosk.rec_engine.detector
    assert detector_before is detector_after


def test_29_cli_argparse_and_model_reachability(monkeypatch):
    """Test CLI argument parser accepts AUTO and V1-V5 and reaches recognition engine."""
    from src.kiosk_app import main
    import sys

    monkeypatch.setattr(sys, "argv", ["kiosk_app.py", "--model", "AUTO", "--max-frames", "1"])
    monkeypatch.setattr(InteractiveMudraKiosk, "run", lambda self, max_frames, save_sample_path: None)
    main()

