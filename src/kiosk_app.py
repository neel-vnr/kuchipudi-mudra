#!/usr/init/env python3
"""
src/kiosk_app.py - Interactive Kuchipudi Mudra Learning Studio (Part 8 - Redesigned).

CORRECTED AUDIT COMPLIANCE & UI REDESIGN:
- Default production model: V1 Baseline SVM (highest held-out benchmark accuracy).
- Model selector removed from normal learner UI (available via debug toggle).
- Robust 21-point MediaPipe landmark visualization with full hand connection topology.
- Corrected reference panel header: "REFERENCE POSE — PENDING INSTRUCTOR VALIDATION".
- Event-driven non-repeating voice feedback with Mute/Unmute control and 5s cooldown.
- Professional dark studio interface with touch-friendly controls.
"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path
import sys
import threading
import time
from typing import Any, Callable, Dict, List, Optional, Tuple

import cv2
import numpy as np

src_dir = Path(__file__).resolve().parent
if str(src_dir) not in sys.path:
    sys.path.insert(0, str(src_dir))

from correction_engine import (
    FingerStatus,
    MudraCorrectionEngine,
    MudraCorrectionReport,
    MudraEvaluationState,
)
from features import HAND_CONNECTIONS, draw_hand_landmarks_on_image
from recognition_engine import (
    MudraRecognitionEngine,
    MudraRecognitionResult,
)

try:
    import sounddevice as sd
    AUDIO_AVAILABLE = True
except Exception:
    AUDIO_AVAILABLE = False

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger("KioskApp")


# ==============================================================================
# AUDIO & VOICE FEEDBACK MANAGER (Event-Driven & Cooldown)
# ==============================================================================

class AudioFeedbackManager:
    """Event-driven speech and audio chime manager with cooldown and mute controls."""

    def __init__(self, sample_rate: int = 44100, enabled: bool = True) -> None:
        self.sample_rate = sample_rate
        self.enabled = enabled
        self.muted = False
        self.last_state: Optional[MudraEvaluationState] = None
        self.last_spoken_instruction: str = ""
        self.last_spoken_time: float = 0.0
        self.cooldown: float = 5.0  # seconds between automatic spoken reminders

    def play_tone(self, frequencies: List[float], durations: List[float]) -> None:
        if not self.enabled or self.muted or not AUDIO_AVAILABLE:
            return

        def _worker():
            try:
                tones = []
                for freq, dur in zip(frequencies, durations):
                    t = np.linspace(0, dur, int(self.sample_rate * dur), endpoint=False)
                    envelope = np.sin(np.pi * np.linspace(0, 1, len(t)))
                    wave = 0.3 * np.sin(2 * np.pi * freq * t) * envelope
                    tones.append(wave)
                audio_data = np.concatenate(tones).astype(np.float32)
                sd.play(audio_data, self.sample_rate)
                sd.wait()
            except Exception as e:
                logger.debug(f"Audio playback error: {e}")

        threading.Thread(target=_worker, daemon=True).start()

    def speak(self, text: str, force: bool = False) -> None:
        """Speaks instruction text asynchronously only on event change or after cooldown."""
        if not self.enabled or self.muted or not text:
            return

        now = time.time()
        if not force and text == self.last_spoken_instruction and (now - self.last_spoken_time) < self.cooldown:
            return

        self.last_spoken_instruction = text
        self.last_spoken_time = now

        def _speech_worker():
            try:
                import subprocess
                clean_text = text.replace('"', '').replace("'", "")
                cmd = f'PowerShell -Command "Add-Type –AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak(\'{clean_text}\');"'
                subprocess.run(cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception as e:
                logger.debug(f"Speech error: {e}")

        threading.Thread(target=_speech_worker, daemon=True).start()

    def notify_event(self, state: MudraEvaluationState, instruction: str = "") -> None:
        """Triggers audio cues and speech only when state or instruction changes."""
        if state != self.last_state:
            self.last_state = state
            if state == MudraEvaluationState.STATE_A_CORRECT:
                self.play_tone([523.25, 659.25, 783.99], [0.10, 0.10, 0.25])
                self.speak("Correct mudra! Excellent posture.", force=True)
            elif state == MudraEvaluationState.STATE_B_WRONG_MUDRA:
                self.play_tone([392.00, 293.66], [0.12, 0.20])
                if instruction:
                    self.speak(instruction, force=True)
            elif state == MudraEvaluationState.STATE_C_INCORRECT_EXECUTION:
                self.play_tone([440.00, 440.00], [0.08, 0.15])
                if instruction:
                    self.speak(instruction, force=False)
            elif state == MudraEvaluationState.STATE_D_UNABLE_TO_ASSESS:
                pass
        else:
            # State unchanged; check if instruction changed or cooldown expired
            if instruction and instruction != self.last_spoken_instruction:
                now = time.time()
                if (now - self.last_spoken_time) >= self.cooldown:
                    self.speak(instruction, force=False)

    def notify_state(self, state: MudraEvaluationState, instruction: str = "") -> None:
        """Alias for notify_event for compatibility with test suite."""
        self.notify_event(state, instruction)


# ==============================================================================
# UI BUTTON COMPONENT
# ==============================================================================

class KioskButton:
    """Touch-friendly UI button."""

    def __init__(
        self,
        rect: Tuple[int, int, int, int],  # (x, y, w, h)
        label: str,
        callback: Callable[[], None],
        bg_color: Tuple[int, int, int] = (50, 50, 50),
        active_color: Tuple[int, int, int] = (0, 140, 255),
        text_color: Tuple[int, int, int] = (255, 255, 255),
    ) -> None:
        self.rect = rect
        self.label = label
        self.callback = callback
        self.bg_color = bg_color
        self.active_color = active_color
        self.text_color = text_color
        self.is_hovered = False

    def contains(self, px: int, py: int) -> bool:
        x, y, w, h = self.rect
        return x <= px <= x + w and y <= py <= y + h

    def draw(self, canvas: np.ndarray) -> None:
        x, y, w, h = self.rect
        color = self.active_color if self.is_hovered else self.bg_color

        cv2.rectangle(canvas, (x, y), (x + w, y + h), color, -1)
        cv2.rectangle(canvas, (x, y), (x + w, y + h), (180, 180, 180), 1)

        font_scale = 0.50
        thickness = 1
        t_size, _ = cv2.getTextSize(self.label, cv2.FONT_HERSHEY_SIMPLEX, font_scale, thickness)
        tx = x + max(4, (w - t_size[0]) // 2)
        ty = y + (h + t_size[1]) // 2 - 2

        cv2.putText(
            canvas,
            self.label,
            (tx, ty),
            cv2.FONT_HERSHEY_SIMPLEX,
            font_scale,
            self.text_color,
            thickness,
            cv2.LINE_AA,
        )


# ==============================================================================
# MAIN KIOSK APPLICATION
# ==============================================================================

class InteractiveMudraKiosk:
    """Interactive Kuchipudi Mudra Learning Studio Kiosk."""

    def __init__(
        self,
        camera_index: Optional[int] = 0,
        default_model: str = "V1",  # V1 is default production model
        screen_size: Tuple[int, int] = (1280, 720),
        enable_audio: bool = True,
    ) -> None:
        self.screen_w, self.screen_h = screen_size
        self.camera_index = camera_index
        self.enable_audio = enable_audio
        self.debug_mode = False

        logger.info(f"Initializing Recognition Engine with default production model: {default_model}")
        self.rec_engine = MudraRecognitionEngine(default_model=default_model)

        logger.info("Initializing Correction Engine...")
        self.corr_engine = MudraCorrectionEngine()

        self.audio_manager = AudioFeedbackManager(enabled=enable_audio)

        self.supported_mudras = self.corr_engine.get_supported_mudras()
        if not self.supported_mudras:
            self.supported_mudras = ["1-Pataka"]

        self.current_mudra_idx = 0
        self.current_model_key = default_model
        self.last_report: Optional[MudraCorrectionReport] = None

        self.cached_ref_img: Optional[np.ndarray] = None
        self.cached_ref_mudra: str = ""
        self.show_reference_skeleton: bool = False

        self.buttons: List[KioskButton] = []
        self._init_buttons()

        self.window_name = "Kuchipudi Mudra Learning Studio"

    def close(self) -> None:
        if hasattr(self, "rec_engine") and self.rec_engine is not None:
            self.rec_engine.close()

    @property
    def current_expected_mudra(self) -> str:
        return self.supported_mudras[self.current_mudra_idx]

    def _init_buttons(self) -> None:
        btn_y = self.screen_h - 52
        btn_h = 42

        self.btn_prev = KioskButton(
            rect=(15, btn_y, 130, btn_h),
            label="< Previous",
            callback=self.previous_mudra,
            bg_color=(45, 45, 45),
            active_color=(0, 140, 255),
        )

        self.btn_next = KioskButton(
            rect=(155, btn_y, 130, btn_h),
            label="Next Mudra >",
            callback=self.next_mudra,
            bg_color=(45, 45, 45),
            active_color=(0, 140, 255),
        )

        self.btn_try_again = KioskButton(
            rect=(295, btn_y, 130, btn_h),
            label="Try Again",
            callback=self.try_again,
            bg_color=(45, 45, 45),
            active_color=(0, 200, 100),
        )

        self.btn_repeat = KioskButton(
            rect=(435, btn_y, 140, btn_h),
            label="Repeat Voice",
            callback=self.repeat_instruction,
            bg_color=(45, 45, 45),
            active_color=(180, 100, 255),
        )

        self.btn_mute = KioskButton(
            rect=(585, btn_y, 130, btn_h),
            label="Mute Audio",
            callback=self.toggle_mute,
            bg_color=(45, 45, 45),
            active_color=(200, 60, 60),
        )

        self.buttons = [
            self.btn_prev,
            self.btn_next,
            self.btn_try_again,
            self.btn_repeat,
            self.btn_mute,
        ]

    def next_mudra(self) -> None:
        self.current_mudra_idx = (self.current_mudra_idx + 1) % len(self.supported_mudras)
        self.cached_ref_img = None
        self.cached_ref_mudra = ""
        self.try_again()
        logger.info(f"Switched to next mudra: {self.current_expected_mudra}")

    def previous_mudra(self) -> None:
        self.current_mudra_idx = (self.current_mudra_idx - 1) % len(self.supported_mudras)
        self.cached_ref_img = None
        self.cached_ref_mudra = ""
        self.try_again()
        logger.info(f"Switched to previous mudra: {self.current_expected_mudra}")

    def toggle_reference_skeleton(self) -> None:
        self.show_reference_skeleton = not self.show_reference_skeleton
        self.cached_ref_img = None
        logger.info(f"Reference skeleton overlay toggled: {self.show_reference_skeleton}")

    def try_again(self) -> None:
        self.rec_engine.clear_smoothing()
        self.audio_manager.last_state = None
        self.audio_manager.last_spoken_instruction = ""
        logger.info("Pose recognition reset for retry.")

    def repeat_instruction(self) -> None:
        if self.last_report and self.last_report.instructions:
            self.audio_manager.speak(self.last_report.instructions[0], force=True)

    def toggle_mute(self) -> None:
        self.audio_manager.muted = not self.audio_manager.muted
        self.btn_mute.label = "Unmute Audio" if self.audio_manager.muted else "Mute Audio"
        self.btn_mute.active_color = (0, 180, 100) if self.audio_manager.muted else (200, 60, 60)
        logger.info(f"Audio muted: {self.audio_manager.muted}")

    def cycle_model(self) -> None:
        """Cycles active recognition model internally for admin/debug evaluation."""
        keys = list(self.rec_engine.AVAILABLE_MODELS.keys())
        idx = keys.index(self.current_model_key)
        new_key = keys[(idx + 1) % len(keys)]
        self.current_model_key = new_key
        self.rec_engine.set_active_model(new_key)
        self.try_again()
        logger.info(f"Model cycled internally to: {self.rec_engine.AVAILABLE_MODELS[new_key]['name']}")

    def handle_mouse_event(self, event: int, x: int, y: int, flags: int, param: Any) -> None:
        if event == cv2.EVENT_MOUSEMOVE:
            for btn in self.buttons:
                btn.is_hovered = btn.contains(x, y)
        elif event == cv2.EVENT_LBUTTONDOWN:
            for btn in self.buttons:
                if btn.contains(x, y):
                    btn.callback()
                    break

    def get_reference_visual(self, expected_mudra: str, target_size: Tuple[int, int]) -> np.ndarray:
        """Loads and prepares reference visualization with correct aspect ratio and panel heading."""
        tw, th = target_size
        if self.cached_ref_mudra == expected_mudra and self.cached_ref_img is not None:
            return cv2.resize(self.cached_ref_img, (tw, th))

        header_h = 44
        footer_h = 50
        content_h = th - header_h - footer_h

        ref_data = self.corr_engine.get_reference_data(expected_mudra)
        vis = np.full((content_h, tw, 3), 20, dtype=np.uint8)
        image_loaded = False
        val_status = "pending_instructor_validation"
        source_attribution = "Bharatanatyam Dataset (Kokul et al.) — Provisional Visual Example"

        if ref_data:
            meta = ref_data.get("metadata", {})
            val_status = meta.get("instructor_validation_status", "pending_instructor_validation")
            img_path = Path(ref_data["image_path"])
            if img_path.is_file():
                raw_img = cv2.imread(str(img_path))
                if raw_img is not None and raw_img.size > 0:
                    image_loaded = True
                    ih, iw = raw_img.shape[:2]
                    scale = min(tw / iw, content_h / ih)
                    nw, nh = max(1, int(iw * scale)), max(1, int(ih * scale))
                    resized = cv2.resize(raw_img, (nw, nh), interpolation=cv2.INTER_LANCZOS4)

                    # Optional reference skeleton overlay when genuine landmarks are available
                    if self.show_reference_skeleton and "landmarks" in ref_data:
                        lms = ref_data["landmarks"]
                        resized = draw_hand_landmarks_on_image(
                            resized,
                            lms,
                            draw_connections=True,
                            draw_points=True,
                            connection_color=(0, 220, 100),
                            point_color=(0, 255, 255),
                            point_radius=4,
                            line_thickness=2,
                        )

                    # Center inside content viewport proportionally
                    ox = (tw - nw) // 2
                    oy = (content_h - nh) // 2
                    vis[oy : oy + nh, ox : ox + nw] = resized

        if not image_loaded:
            # Display clear message when genuine reference image is unavailable
            cv2.rectangle(vis, (tw // 2 - 200, content_h // 2 - 45), (tw // 2 + 200, content_h // 2 + 45), (32, 32, 32), -1)
            cv2.rectangle(vis, (tw // 2 - 200, content_h // 2 - 45), (tw // 2 + 200, content_h // 2 + 45), (60, 60, 60), 1)
            cv2.putText(
                vis,
                "REFERENCE IMAGE NOT AVAILABLE",
                (tw // 2 - 165, content_h // 2 - 5),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.58,
                (0, 0, 240),
                2,
                cv2.LINE_AA,
            )
            cv2.putText(
                vis,
                "Awaiting authentic instructor reference upload",
                (tw // 2 - 165, content_h // 2 + 25),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.42,
                (170, 170, 170),
                1,
                cv2.LINE_AA,
            )

        # Assemble full panel: Header + Photo Content + Footer
        panel = np.full((th, tw, 3), 18, dtype=np.uint8)
        panel[header_h : header_h + content_h, :] = vis

        # 1. Header banner with required warning text
        cv2.rectangle(panel, (0, 0), (tw, header_h), (25, 25, 25), -1)
        cv2.line(panel, (0, header_h), (tw, header_h), (60, 60, 60), 1)
        cv2.putText(
            panel,
            "PROVISIONAL VISUAL EXAMPLE — PENDING KUCHIPUDI INSTRUCTOR VALIDATION",
            (12, 28),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            (0, 165, 255),  # Amber warning color
            1,
            cv2.LINE_AA,
        )

        # 2. Footer banner with Mudra Name, Status, and Attribution
        footer_y = header_h + content_h
        cv2.rectangle(panel, (0, footer_y), (tw, th), (22, 22, 22), -1)
        cv2.line(panel, (0, footer_y), (tw, footer_y), (50, 50, 50), 1)

        status_display = "Instructor Verified" if val_status == "instructor_verified" else "Pending Instructor Validation"
        cv2.putText(
            panel,
            f"Mudra: {expected_mudra}  |  Status: {status_display}",
            (12, footer_y + 20),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.45,
            (255, 255, 255),
            1,
            cv2.LINE_AA,
        )

        cv2.putText(
            panel,
            f"Source: {source_attribution}",
            (12, footer_y + 40),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.38,
            (160, 160, 160),
            1,
            cv2.LINE_AA,
        )

        self.cached_ref_mudra = expected_mudra
        self.cached_ref_img = panel
        return panel

    def render_kiosk_frame(
        self,
        camera_frame: np.ndarray,
        rec_result: MudraRecognitionResult,
        report: MudraCorrectionReport,
        fps: float,
    ) -> np.ndarray:
        """Composes the professional dark studio interface layout."""
        canvas = np.full((self.screen_h, self.screen_w, 3), 15, dtype=np.uint8)

        # 1. TOP HEADER BANNER
        header_h = 60
        cv2.rectangle(canvas, (0, 0), (self.screen_w, header_h), (22, 22, 22), -1)
        cv2.line(canvas, (0, header_h), (self.screen_w, header_h), (45, 45, 45), 2)

        cv2.putText(
            canvas,
            "KUCHIPUDI MUDRA LEARNING STUDIO",
            (20, 38),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.80,
            (255, 255, 255),
            2,
            cv2.LINE_AA,
        )

        lesson_text = f"Lesson: {self.current_expected_mudra}"
        cv2.putText(
            canvas,
            lesson_text,
            (480, 38),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.65,
            (0, 220, 100),
            2,
            cv2.LINE_AA,
        )

        cam_fps_text = f"Cam: {self.camera_index} | FPS: {fps:.1f}"
        cv2.putText(
            canvas,
            cam_fps_text,
            (self.screen_w - 210, 38),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.52,
            (180, 180, 180),
            1,
            cv2.LINE_AA,
        )

        # 2. DUAL-PANE VIEWPORT
        pane_y = header_h + 10
        pane_h = 445
        pane_w = (self.screen_w - 30) // 2

        # Left Panel: Live Performance & Skeleton
        left_x = 10
        annotated_cam = camera_frame.copy()
        h_cam, w_cam = annotated_cam.shape[:2]

        landmarks_to_draw = rec_result.hand_landmarks if rec_result.hand_landmarks is not None else rec_result.landmarks_21_3
        if rec_result.hand_detected and landmarks_to_draw is not None:
            # Draw corrected skeleton with color-coded fingers
            annotated_cam = self.corr_engine.draw_correction_skeleton(
                annotated_cam,
                landmarks_to_draw,
                report.finger_statuses,
            )

        cam_resized = cv2.resize(annotated_cam, (pane_w, pane_h))
        canvas[pane_y : pane_y + pane_h, left_x : left_x + pane_w] = cam_resized
        cv2.rectangle(canvas, (left_x, pane_y), (left_x + pane_w, pane_y + pane_h), (60, 60, 60), 1)

        # Left Pane Sub-header
        cv2.rectangle(canvas, (left_x, pane_y), (left_x + pane_w, pane_y + 35), (20, 20, 20), -1)
        hand_status_text = f"● Hand Detected ({rec_result.handedness})" if rec_result.hand_detected else "○ No Hand Detected"
        hand_status_color = (0, 220, 100) if rec_result.hand_detected else (0, 165, 255)

        cv2.putText(
            canvas,
            "LIVE CAM",
            (left_x + 12, pane_y + 23),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.50,
            (0, 220, 255),
            1,
            cv2.LINE_AA,
        )
        cv2.putText(
            canvas,
            hand_status_text,
            (left_x + 105, pane_y + 23),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            hand_status_color,
            1,
            cv2.LINE_AA,
        )

        # If no hand detected, display prominent message on live feed
        if not rec_result.hand_detected:
            cv2.putText(
                canvas,
                "Show your hand to the camera",
                (left_x + pane_w // 2 - 150, pane_y + pane_h // 2),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.65,
                (0, 160, 255),
                2,
                cv2.LINE_AA,
            )

        # Debug Overlay (when enabled via 'd' key)
        if self.debug_mode:
            l0_norm = "N/A"
            l0_px = "N/A"
            if rec_result.hand_detected and rec_result.hand_landmarks and len(rec_result.hand_landmarks) > 0:
                l0 = rec_result.hand_landmarks[0]
                l0_norm = f"({l0.x:.3f}, {l0.y:.3f})"
                l0_px = f"({int(l0.x * w_cam)}, {int(l0.y * h_cam)})"

            dbg_lines = [
                f"Orig Frame Res: {w_cam}x{h_cam}",
                f"Detection Input: {w_cam}x{h_cam}",
                f"Display Pane Res: {pane_w}x{pane_h}",
                f"Hands Detected: {1 if rec_result.hand_detected else 0}",
                f"L0 Norm: {l0_norm} | Px: {l0_px}",
            ]
            dy = pane_y + pane_h - 110
            for line in dbg_lines:
                cv2.putText(canvas, line, (left_x + 12, dy), cv2.FONT_HERSHEY_SIMPLEX, 0.38, (0, 255, 0), 1, cv2.LINE_AA)
                dy += 18

        # Right Panel: Reference Pose
        right_x = left_x + pane_w + 10
        ref_visual = self.get_reference_visual(self.current_expected_mudra, (pane_w, pane_h))
        canvas[pane_y : pane_y + pane_h, right_x : right_x + pane_w] = ref_visual
        cv2.rectangle(canvas, (right_x, pane_y), (right_x + pane_w, pane_y + pane_h), (60, 60, 60), 1)

        # 3. BOTTOM INFORMATION PANEL
        status_y = pane_y + pane_h + 10
        status_h = self.screen_h - status_y - 58
        cv2.rectangle(canvas, (10, status_y), (self.screen_w - 10, status_y + status_h), (25, 25, 25), -1)
        cv2.rectangle(canvas, (10, status_y), (self.screen_w - 10, status_y + status_h), (50, 50, 50), 1)

        state_colors = {
            MudraEvaluationState.STATE_A_CORRECT: (0, 200, 80),            # Green
            MudraEvaluationState.STATE_B_WRONG_MUDRA: (0, 0, 230),          # Red
            MudraEvaluationState.STATE_C_INCORRECT_EXECUTION: (0, 160, 255),# Amber/Yellow
            MudraEvaluationState.STATE_D_UNABLE_TO_ASSESS: (160, 160, 160), # Slate/Gray
        }
        accent = state_colors.get(report.state, (100, 100, 100))
        cv2.rectangle(canvas, (10, status_y), (18, status_y + status_h), accent, -1)

        # Expected vs Recognized labels
        cv2.putText(canvas, f"Expected Mudra: {self.current_expected_mudra}", (25, status_y + 24), cv2.FONT_HERSHEY_SIMPLEX, 0.58, (255, 255, 255), 1, cv2.LINE_AA)

        rec_color = (0, 220, 100) if rec_result.is_expected_match else (0, 140, 255)
        rec_display = f"Recognized Mudra: {rec_result.predicted_mudra} ({rec_result.confidence*100:.1f}%)" if rec_result.hand_detected else "Recognized Mudra: Searching..."
        cv2.putText(canvas, rec_display, (25, status_y + 48), cv2.FONT_HERSHEY_SIMPLEX, 0.58, rec_color, 1, cv2.LINE_AA)

        # Evaluation Status
        cv2.putText(canvas, f"Status: {report.state.value}", (420, status_y + 24), cv2.FONT_HERSHEY_SIMPLEX, 0.62, accent, 2, cv2.LINE_AA)

        # Finger Feedback Chips
        chip_x = 420
        chip_y = status_y + 36
        for f_name, f_stat in report.finger_statuses.items():
            col = (0, 200, 80) if f_stat == FingerStatus.CORRECT else ((0, 160, 255) if f_stat == FingerStatus.NEEDS_ADJUSTMENT else (0, 0, 220))
            cv2.rectangle(canvas, (chip_x, chip_y), (chip_x + 65, chip_y + 18), (40, 40, 40), -1)
            cv2.rectangle(canvas, (chip_x, chip_y), (chip_x + 65, chip_y + 18), col, 1)
            cv2.putText(canvas, f"{f_name[:3]}: {f_stat.value[0]}", (chip_x + 5, chip_y + 13), cv2.FONT_HERSHEY_SIMPLEX, 0.35, col, 1, cv2.LINE_AA)
            chip_x += 72

        # Instruction Text
        inst_text = report.instructions[0] if report.instructions else report.explanation
        cv2.putText(canvas, f"Instruction: {inst_text}", (25, status_y + 80), cv2.FONT_HERSHEY_SIMPLEX, 0.52, (240, 240, 240), 1, cv2.LINE_AA)

        # 4. DRAW CONTROLS
        for btn in self.buttons:
            btn.draw(canvas)

        return canvas

    def run(self, max_frames: Optional[int] = None, save_sample_path: Optional[Path] = None) -> None:
        print("=" * 60)
        print("STARTING KUCHIPUDI MUDRA LEARNING STUDIO")
        print("Controls: 'n' (Next), 'p' (Prev), 't' (Try Again), 'r' (Repeat), 'm' (Mute), 's' (Skeleton), 'q' (Quit)")
        print("=" * 60)

        cap = cv2.VideoCapture(self.camera_index if self.camera_index is not None else 0)
        if not cap.isOpened():
            logger.warning("Could not open camera. Running with test simulation feed...")
            cap = None

        cv2.namedWindow(self.window_name, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(self.window_name, self.screen_w, self.screen_h)
        cv2.setMouseCallback(self.window_name, self.handle_mouse_event)

        frames_count = 0
        prev_time = time.time()
        fps = 0.0

        try:
            while True:
                if cap is not None and cap.isOpened():
                    ret, frame = cap.read()
                    if not ret or frame is None:
                        frame = np.full((480, 640, 3), 35, dtype=np.uint8)
                else:
                    frame = np.full((480, 640, 3), 35, dtype=np.uint8)

                frames_count += 1
                curr_time = time.time()
                fps = 1.0 / max(1e-5, curr_time - prev_time)
                prev_time = curr_time

                # 1. Recognition
                rec_result, _ = self.rec_engine.process_frame(
                    frame,
                    expected_mudra=self.current_expected_mudra,
                )

                # 2. Correction & 4-State Analysis
                report = self.corr_engine.evaluate_pose(
                    rec_result,
                    expected_mudra=self.current_expected_mudra,
                )
                self.last_report = report

                # 3. Event-Driven Audio Feedback
                if rec_result.hand_detected:
                    lead_inst = report.instructions[0] if report.instructions else ""
                    self.audio_manager.notify_event(report.state, lead_inst)

                # 4. Render UI
                canvas = self.render_kiosk_frame(frame, rec_result, report, fps)

                if save_sample_path and frames_count == 1:
                    save_sample_path.parent.mkdir(parents=True, exist_ok=True)
                    cv2.imwrite(str(save_sample_path), canvas)
                    logger.info(f"Saved kiosk UI screenshot to: {save_sample_path}")

                try:
                    cv2.imshow(self.window_name, canvas)
                    key = cv2.waitKey(1) & 0xFF
                    if key in (ord("q"), ord("Q"), 27):
                        break
                    elif key in (ord("n"), ord("N")):
                        self.next_mudra()
                    elif key in (ord("p"), ord("P")):
                        self.previous_mudra()
                    elif key in (ord("t"), ord("T")):
                        self.try_again()
                    elif key in (ord("r"), ord("R")):
                        self.repeat_instruction()
                    elif key in (ord("m"), ord("M")):
                        self.toggle_mute()
                    elif key in (ord("s"), ord("S")):
                        self.toggle_reference_skeleton()
                    elif key in (ord("d"), ord("D")):
                        self.debug_mode = not self.debug_mode
                except Exception:
                    pass

                if max_frames is not None and frames_count >= max_frames:
                    logger.info(f"Reached max frames test limit ({max_frames}). Stopping.")
                    if save_sample_path:
                        cv2.imwrite(str(save_sample_path), canvas)
                    break

        finally:
            if cap is not None:
                cap.release()
            self.close()
            try:
                cv2.destroyAllWindows()
            except Exception:
                pass


def main():
    parser = argparse.ArgumentParser(description="Kuchipudi Mudra Learning Studio Kiosk")
    parser.add_argument("--camera", type=int, default=0, help="Camera index")
    parser.add_argument("--model", type=str, default="V1", choices=["V1", "V2", "V3", "V4", "V5"], help="Default model (V1 default)")
    parser.add_argument("--max-frames", type=int, default=None, help="Max frames for test mode")
    parser.add_argument("--save-sample", type=Path, default=Path("reports/kiosk_ui_fixed.png"), help="Path to save fixed UI screenshot")
    args = parser.parse_args()

    kiosk = InteractiveMudraKiosk(camera_index=args.camera, default_model=args.model)
    kiosk.run(max_frames=args.max_frames, save_sample_path=args.save_sample)


if __name__ == "__main__":
    main()
