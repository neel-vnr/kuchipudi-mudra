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
- Model Evaluation Dashboard: toggle with 'e' key, displays real-time metrics from
  reports/ artifacts with live pipeline performance benchmarks (Parts C & D).
"""

from __future__ import annotations

import argparse
import base64
import json
import logging
import tempfile
from pathlib import Path
import queue
import subprocess
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
from evaluation_dashboard import EvaluationDashboard
from mudra_learning_db import MudraLearningDatabase, MudraLearningEntry

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
    """Thread-safe, single-worker ordered speech queue manager with cancellable Popen subprocess and immediate mute."""

    def __init__(self, sample_rate: int = 44100, enabled: bool = True, language: str = "en") -> None:
        self.sample_rate = sample_rate
        self.enabled = enabled
        self.muted = False
        self.language = language
        self.last_state: Optional[MudraEvaluationState] = None
        self.last_spoken_instruction: str = ""
        self.last_spoken_time: float = 0.0
        self.cooldown: float = 5.0
        self.queue: queue.Queue = queue.Queue()
        self.running = True
        self.lock = threading.RLock()
        self.active_process: Optional[subprocess.Popen] = None
        self._worker_thread = threading.Thread(target=self._speech_worker_loop, daemon=True)
        self._worker_thread.start()

    def play_tone(self, frequencies: List[float], durations: List[float]) -> None:
        if not self.enabled or self.muted or not AUDIO_AVAILABLE:
            return

        def _tone_worker():
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
                logger.debug(f"Audio tone playback error: {e}")

        threading.Thread(target=_tone_worker, daemon=True).start()

    def speak(self, text: str, force: bool = False, lang: Optional[str] = None) -> None:
        if not self.enabled or self.muted or not text:
            return

        with self.lock:
            now = time.time()
            if not force and text == self.last_spoken_instruction and (now - self.last_spoken_time) < self.cooldown:
                return
            self.last_spoken_instruction = text
            self.last_spoken_time = now
            active_lang = lang or self.language
            self.queue.put((text, force, active_lang))

    def clear_queue(self) -> None:
        with self.lock:
            while not self.queue.empty():
                try:
                    self.queue.get_nowait()
                    self.queue.task_done()
                except Exception:
                    break

    def cancel_current_audio(self) -> None:
        """Cancels active speech process and clears pending queue items."""
        with self.lock:
            self.clear_queue()
            if self.active_process is not None:
                try:
                    self.active_process.terminate()
                    self.active_process.wait(timeout=1.0)
                except Exception:
                    try:
                        self.active_process.kill()
                    except Exception:
                        pass
                self.active_process = None

    def mute(self) -> None:
        with self.lock:
            self.muted = True
            self.cancel_current_audio()
        logger.info("Audio muted. Active speech terminated and queue cleared.")

    def unmute(self) -> None:
        with self.lock:
            self.muted = False
        logger.info("Audio unmuted.")

    def toggle_mute(self) -> bool:
        if self.muted:
            self.unmute()
        else:
            self.mute()
        return self.muted

    def stop(self) -> None:
        with self.lock:
            self.running = False
            self.cancel_current_audio()

    def _speech_worker_loop(self) -> None:
        while self.running:
            try:
                try:
                    item = self.queue.get(timeout=0.2)
                except queue.Empty:
                    continue

                if item is None:
                    continue

                text, force, lang = item

                with self.lock:
                    if not self.enabled or self.muted:
                        self.queue.task_done()
                        continue

                clean_text = text.replace('"', '""').replace("'", "''")
                ps_script = (
                    f"$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
                    f"$synth.Volume = 100; "
                    f"if ('{lang}' -eq 'te') {{ "
                    f"  $v = $synth.GetInstalledVoices() | Where-Object {{ $_.VoiceInfo.Culture.Name -like 'te*' }} | Select-Object -First 1; "
                    f"  if ($v) {{ $synth.SelectVoice($v.VoiceInfo.Name) }} "
                    f"}} "
                    f"$synth.Speak('{clean_text}');"
                )
                encoded_cmd = base64.b64encode(ps_script.encode("utf-16le")).decode("ascii")

                with self.lock:
                    if not self.running or self.muted:
                        self.queue.task_done()
                        continue
                    proc = subprocess.Popen(
                        ["powershell.exe", "-NoProfile", "-EncodedCommand", encoded_cmd],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                    self.active_process = proc

                while proc.poll() is None:
                    time.sleep(0.05)
                    with self.lock:
                        if not self.running or self.muted:
                            try:
                                proc.terminate()
                                proc.wait(timeout=0.5)
                            except Exception:
                                try:
                                    proc.kill()
                                except Exception:
                                    pass
                            break

                with self.lock:
                    if self.active_process == proc:
                        self.active_process = None
                self.queue.task_done()

            except Exception as e:
                logger.debug(f"Speech worker error: {e}")
                with self.lock:
                    self.active_process = None

    def speak_welcome(self) -> None:
        self.speak("Welcome to MudraGuru, your offline intelligent Mudra learning studio.", force=True)

    def speak_lesson_intro(self, entry: Any) -> None:
        script = f"Lesson on {entry.traditional_name}. Meaning: {entry.meaning}. {entry.formation_instructions}."
        self.speak(script, force=True)

    def speak_challenge_prompt(self, entry: Any) -> None:
        script = f"Random Challenge! Can you perform {entry.traditional_name}? {entry.formation_instructions}. Show your hand to the camera."
        self.speak(script, force=True)

    def speak_recognition_feedback(self, state: MudraEvaluationState, expected: str, recognized: str, is_match: bool, has_tolerances: bool) -> None:
        if state == MudraEvaluationState.STATE_D_UNABLE_TO_ASSESS:
            self.speak("Unable to assess. Please position your hand clearly in front of the camera.", force=False)
        elif state == MudraEvaluationState.STATE_B_WRONG_MUDRA:
            self.speak(f"Wrong mudra. You performed {recognized}, but target is {expected}. Please try again.", force=False)
        elif is_match and not has_tolerances:
            self.speak(f"Mudra class recognized as {recognized}. Pose validation is pending Kuchipudi instructor review.", force=False)
        elif state == MudraEvaluationState.STATE_A_CORRECT:
            self.speak("Correct mudra! Excellent posture.", force=True)

    def notify_event(self, state: MudraEvaluationState, instruction: str = "") -> None:
        if state != self.last_state:
            self.last_state = state
            if state == MudraEvaluationState.STATE_A_CORRECT:
                self.play_tone([523.25, 659.25, 783.99], [0.10, 0.10, 0.25])
            elif state == MudraEvaluationState.STATE_B_WRONG_MUDRA:
                self.play_tone([392.00, 293.66], [0.12, 0.20])
            elif state == MudraEvaluationState.STATE_C_INCORRECT_EXECUTION:
                self.play_tone([440.00, 440.00], [0.08, 0.15])
            elif state == MudraEvaluationState.STATE_D_UNABLE_TO_ASSESS:
                pass
        else:
            if instruction and instruction != self.last_spoken_instruction:
                now = time.time()
                if (now - self.last_spoken_time) >= self.cooldown:
                    self.speak(instruction, force=False)

    def notify_state(self, state: MudraEvaluationState, instruction: str = "") -> None:
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

        # Dedicated Model Evaluation Dashboard & Live Performance Tracker
        self.eval_dashboard = EvaluationDashboard(default_model=default_model)
        self.learning_db = MudraLearningDatabase()
        self.current_tab = "HOME"  # "HOME", "LIVE", "LIBRARY", "CHALLENGE", "EVAL"
        self.mode = "learn"  # "learn" or "challenge"
        self.show_all_grid = False
        self.challenge_mudra = ""
        self.last_random_mudra = ""
        self.fps: float = 0.0
        self.perf_metrics: Dict[str, float] = {
            "inference_latency_ms": 0.0,
            "inference_ms": 0.0,
            "correction_latency_ms": 0.0,
            "correction_ms": 0.0,
            "total_processing_ms": 0.0,
            "total_proc_ms": 0.0,
            "camera_fps": 0.0,
            "processing_throughput_fps": 0.0,
            "throughput_fps": 0.0,
        }

        self.window_name = "Kuchipudi Mudra Learning Studio"
        self.buttons: List[KioskButton] = []
        self._init_buttons()

        # Publishes the most recent prediction for the ESP32 bridge, independent
        # of how often the UI repaints.
        self._last_rec_result: Any = None
        threading.Thread(target=self._bridge_publisher, daemon=True).start()

    def _bridge_publisher(self) -> None:
        while True:
            result = self._last_rec_result
            if result is not None:
                publish_bridge_state(result)
            time.sleep(0.2)

    def close(self) -> None:
        if hasattr(self, "audio_manager") and self.audio_manager is not None:
            self.audio_manager.stop()
            try:
                if self.audio_manager._worker_thread.is_alive():
                    self.audio_manager._worker_thread.join(timeout=1.0)
            except Exception:
                pass
            try:
                if AUDIO_AVAILABLE:
                    sd.stop()
            except Exception:
                pass
        if hasattr(self, "rec_engine") and self.rec_engine is not None:
            self.rec_engine.close()

    @property
    def current_expected_mudra(self) -> str:
        return self.supported_mudras[self.current_mudra_idx]

    def _init_buttons(self) -> None:
        btn_y = self.screen_h - 50
        btn_h = 38

        self.btn_try_again = KioskButton(
            rect=(15, btn_y, 115, btn_h),
            label="Try Again",
            callback=self.try_again,
            bg_color=(45, 45, 45),
            active_color=(0, 200, 100),
        )

        self.btn_all_mudras = KioskButton(
            rect=(138, btn_y, 135, btn_h),
            label="All Mudras [A]",
            callback=self.toggle_all_mudras_grid,
            bg_color=(45, 45, 45),
            active_color=(140, 0, 255),
        )

        self.btn_challenge = KioskButton(
            rect=(281, btn_y, 135, btn_h),
            label="Challenge [C]",
            callback=self.start_random_challenge,
            bg_color=(45, 45, 45),
            active_color=(255, 140, 0),
        )

        self.btn_repeat = KioskButton(
            rect=(424, btn_y, 115, btn_h),
            label="Repeat [R]",
            callback=self.repeat_instruction,
            bg_color=(45, 45, 45),
            active_color=(180, 100, 255),
        )

        self.btn_mute = KioskButton(
            rect=(547, btn_y, 110, btn_h),
            label="Mute Audio",
            callback=self.toggle_mute,
            bg_color=(45, 45, 45),
            active_color=(200, 60, 60),
        )

        self.btn_eval = KioskButton(
            rect=(665, btn_y, 145, btn_h),
            label="Dashboard [E]",
            callback=self.toggle_evaluation_dashboard,
            bg_color=(45, 45, 45),
            active_color=(0, 165, 255),
        )

        self.buttons = [
            self.btn_try_again,
            self.btn_all_mudras,
            self.btn_challenge,
            self.btn_repeat,
            self.btn_mute,
            self.btn_eval,
        ]

    def toggle_all_mudras_grid(self) -> None:
        self.show_all_grid = not self.show_all_grid
        if self.show_all_grid and self.eval_dashboard.is_open:
            self.eval_dashboard.close()
        logger.info(f"All Mudras selection grid toggled: {self.show_all_grid}")

    def select_mudra_by_id(self, mudra_id: str) -> None:
        self.audio_manager.cancel_current_audio()
        if mudra_id in self.supported_mudras:
            self.current_mudra_idx = self.supported_mudras.index(mudra_id)
            self.mode = "learn"
            self.show_all_grid = False
            self.cached_ref_img = None
            self.cached_ref_mudra = ""
            self.try_again()
            entry = self.learning_db.get_entry(mudra_id)
            if entry:
                self.audio_manager.speak_lesson_intro(entry)
            logger.info(f"Selected mudra: {mudra_id}")

    def start_random_challenge(self) -> None:
        self.audio_manager.cancel_current_audio()
        import random
        candidates = [m for m in self.supported_mudras if m != self.current_expected_mudra]
        if not candidates:
            candidates = self.supported_mudras
        chosen = random.choice(candidates)
        self.challenge_mudra = chosen
        self.last_random_mudra = chosen
        self.current_mudra_idx = self.supported_mudras.index(chosen)
        self.mode = "challenge"
        self.show_all_grid = False
        if self.eval_dashboard.is_open:
            self.eval_dashboard.close()
        self.cached_ref_img = None
        self.cached_ref_mudra = ""
        self.try_again()

        entry = self.learning_db.get_entry(chosen)
        if entry:
            self.audio_manager.speak_challenge_prompt(entry)
        logger.info(f"Started Random Challenge for mudra: {chosen}")

    def next_mudra(self) -> None:
        self.audio_manager.cancel_current_audio()
        self.mode = "learn"
        self.show_all_grid = False
        self.current_mudra_idx = (self.current_mudra_idx + 1) % len(self.supported_mudras)
        self.cached_ref_img = None
        self.cached_ref_mudra = ""
        self.try_again()
        entry = self.learning_db.get_entry(self.current_expected_mudra)
        if entry:
            self.audio_manager.speak_lesson_intro(entry)
        logger.info(f"Switched to next mudra: {self.current_expected_mudra}")

    def previous_mudra(self) -> None:
        self.audio_manager.cancel_current_audio()
        self.mode = "learn"
        self.show_all_grid = False
        self.current_mudra_idx = (self.current_mudra_idx - 1) % len(self.supported_mudras)
        self.cached_ref_img = None
        self.cached_ref_mudra = ""
        self.try_again()
        entry = self.learning_db.get_entry(self.current_expected_mudra)
        if entry:
            self.audio_manager.speak_lesson_intro(entry)
        logger.info(f"Switched to previous mudra: {self.current_expected_mudra}")

    def try_again(self) -> None:
        self.audio_manager.cancel_current_audio()
        self.rec_engine.clear_smoothing()
        self.audio_manager.last_state = None
        self.audio_manager.last_spoken_instruction = ""
        logger.info("Pose recognition reset for retry.")

    def set_tab(self, tab: str) -> None:
        if tab == "HOME":
            self.audio_manager.cancel_current_audio()
        elif tab in ("LIVE", "LIBRARY", "EVAL", "CHALLENGE"):
            self.audio_manager.cancel_current_audio()

        self.current_tab = tab
        if tab == "HOME":
            self.try_again()
            self.eval_dashboard.close()
            self.show_all_grid = False
        elif tab == "LIVE":
            self.eval_dashboard.close()
            self.show_all_grid = False
            self.try_again()
        elif tab == "LIBRARY":
            self.eval_dashboard.close()
            self.show_all_grid = True
        elif tab == "CHALLENGE":
            self.eval_dashboard.close()
            self.show_all_grid = False
            self.start_random_challenge()
        elif tab == "EVAL":
            self.show_all_grid = False
            self.eval_dashboard.open()
        logger.info(f"Switched active tab to: {tab}")

    def test_voice(self) -> None:
        """Tests the speech engine with an actual audible test sentence."""
        try:
            self.audio_manager.speak("Voice test successful. Audio engine operational.", force=True)
            logger.info("Test voice invoked successfully.")
        except Exception as e:
            logger.error(f"Test voice error: {e}")

    def toggle_mute(self) -> None:
        muted = self.audio_manager.toggle_mute()
        self.btn_mute.label = "Unmute Audio" if muted else "Mute Audio"
        self.btn_mute.active_color = (0, 180, 100) if muted else (200, 60, 60)
        logger.info(f"Audio muted: {muted}")

    def render_top_tabs(self, canvas: np.ndarray) -> None:
        """Renders the professional top navigation tab bar with 5 tabs (no EVALUATION)."""
        h, w = canvas.shape[:2]
        tab_y = 6
        tab_h = 42
        tabs = [
            ("HOME", "HOME"),
            ("IDENTIFY", "IDENTIFY"),
            ("LIBRARY", "LEARN"),
            ("PRACTICE", "PRACTICE"),
            ("CHALLENGE", "CHALLENGE"),
        ]

        x_cursor = 15
        for t_key, t_label in tabs:
            is_active = (self.current_tab == t_key)
            t_w = 125 if len(t_label) > 10 else 105
            bg_col = (0, 140, 255) if is_active else (35, 35, 35)
            border_col = (255, 255, 255) if is_active else (60, 60, 60)

            cv2.rectangle(canvas, (x_cursor, tab_y), (x_cursor + t_w, tab_y + tab_h), bg_col, -1)
            cv2.rectangle(canvas, (x_cursor, tab_y), (x_cursor + t_w, tab_y + tab_h), border_col, 2 if is_active else 1)

            cv2.putText(
                canvas,
                t_label,
                (x_cursor + 10, tab_y + 26),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.40,
                (255, 255, 255),
                1,
                cv2.LINE_AA,
            )
            x_cursor += t_w + 8

        # Persistent Home button on right
        home_btn_x = w - 135
        cv2.rectangle(canvas, (home_btn_x, tab_y), (home_btn_x + 120, tab_y + tab_h), (40, 100, 180), -1)
        cv2.rectangle(canvas, (home_btn_x, tab_y), (home_btn_x + 120, tab_y + tab_h), (255, 255, 255), 1)
        cv2.putText(
            canvas,
            "🏠 HOME [H]",
            (home_btn_x + 12, tab_y + 26),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            (255, 255, 255),
            1,
            cv2.LINE_AA,
        )

    def render_home_screen(self, canvas: np.ndarray) -> np.ndarray:
        """Renders the professional MudraGuru Home screen with 5 main feature cards and Voice Settings panel."""
        h, w = canvas.shape[:2]
        canvas[:] = (15, 15, 15)

        self.render_top_tabs(canvas)

        cv2.putText(canvas, "MUDRAGURU", (40, 110), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (255, 255, 255), 2, cv2.LINE_AA)
        cv2.putText(canvas, "Offline Intelligent Mudra Learning Studio", (40, 142), cv2.FONT_HERSHEY_SIMPLEX, 0.58, (180, 180, 180), 1, cv2.LINE_AA)

        # 5 Main Feature Cards
        cards = [
            ("1. IDENTIFY MUDRA", "Show any mudra and let AI identify it automatically.", "IDENTIFY", (0, 140, 255)),
            ("2. LEARN ALL MUDRAS", "Explore, understand and practise all 30 mudras.", "LIBRARY", (140, 0, 255)),
            ("3. RANDOM CHALLENGE", "Test yourself with randomized mudras.", "CHALLENGE", (255, 140, 0)),
            ("4. LIVE PRACTICE", "Perform a selected mudra and receive live corrections.", "PRACTICE", (0, 220, 100)),
            ("5. EVALUATION DASHBOARD", "Accuracy, precision, recall, F1, confusion matrix & metrics.", "EVAL", (0, 165, 255)),
        ]

        card_w, card_h = 360, 110
        start_x, start_y = 40, 175

        for idx, (c_title, c_desc, c_tab, c_col) in enumerate(cards):
            r = idx // 3
            c = idx % 3
            if r == 1 and c == 2:
                cx = start_x + 0 * (card_w + 25)
                cy = start_y + 1 * (card_h + 18)
            else:
                cx = start_x + c * (card_w + 25)
                cy = start_y + r * (card_h + 18)

            cv2.rectangle(canvas, (cx, cy), (cx + card_w, cy + card_h), (25, 25, 25), -1)
            cv2.rectangle(canvas, (cx, cy), (cx + card_w, cy + card_h), c_col, 2)

            cv2.putText(canvas, c_title, (cx + 16, cy + 32), cv2.FONT_HERSHEY_SIMPLEX, 0.60, c_col, 2, cv2.LINE_AA)
            cv2.putText(canvas, c_desc[:55], (cx + 16, cy + 62), cv2.FONT_HERSHEY_SIMPLEX, 0.38, (210, 210, 210), 1, cv2.LINE_AA)
            cv2.putText(canvas, "Click card or use top tab", (cx + 16, cy + 90), cv2.FONT_HERSHEY_SIMPLEX, 0.34, (130, 130, 130), 1, cv2.LINE_AA)

        # Voice Settings Panel on Home screen (Right side: x = 800, w = 440)
        vx, vy, vw, vh = 800, 175, 440, 238
        cv2.rectangle(canvas, (vx, vy), (vx + vw, vy + vh), (22, 22, 22), -1)
        cv2.rectangle(canvas, (vx, vy), (vx + vw, vy + vh), (80, 80, 80), 1)

        cv2.putText(canvas, "VOICE SETTINGS & STATUS", (vx + 16, vy + 28), cv2.FONT_HERSHEY_SIMPLEX, 0.50, (0, 220, 255), 1, cv2.LINE_AA)

        v_status = "VOICE MUTED" if self.audio_manager.muted else ("SPEAKING" if self.audio_manager.active_process else "VOICE READY")
        v_col = (200, 60, 60) if self.audio_manager.muted else ((0, 165, 255) if self.audio_manager.active_process else (0, 220, 100))
        cv2.putText(canvas, f"Status: {v_status}", (vx + 16, vy + 60), cv2.FONT_HERSHEY_SIMPLEX, 0.42, v_col, 1, cv2.LINE_AA)
        cv2.putText(canvas, f"Language: English (US) — SAPI Active", (vx + 16, vy + 88), cv2.FONT_HERSHEY_SIMPLEX, 0.38, (200, 200, 200), 1, cv2.LINE_AA)
        cv2.putText(canvas, f"Telugu Voice: Not Installed (Using English)", (vx + 16, vy + 114), cv2.FONT_HERSHEY_SIMPLEX, 0.36, (170, 170, 170), 1, cv2.LINE_AA)

        # Test Voice button inside home panel
        cv2.rectangle(canvas, (vx + 16, vy + 138), (vx + 205, vy + 178), (45, 45, 45), -1)
        cv2.rectangle(canvas, (vx + 16, vy + 138), (vx + 205, vy + 178), (0, 140, 255), 1)
        cv2.putText(canvas, "TEST VOICE", (vx + 45, vy + 163), cv2.FONT_HERSHEY_SIMPLEX, 0.44, (255, 255, 255), 1, cv2.LINE_AA)

        # Mute toggle button inside home panel
        mute_lbl = "UNMUTE" if self.audio_manager.muted else "MUTE"
        mute_col = (0, 180, 100) if self.audio_manager.muted else (200, 60, 60)
        cv2.rectangle(canvas, (vx + 220, vy + 138), (vx + 415, vy + 178), (45, 45, 45), -1)
        cv2.rectangle(canvas, (vx + 220, vy + 138), (vx + 415, vy + 178), mute_col, 1)
        cv2.putText(canvas, mute_lbl, (vx + 270, vy + 163), cv2.FONT_HERSHEY_SIMPLEX, 0.44, (255, 255, 255), 1, cv2.LINE_AA)

        cv2.putText(canvas, "Press 'H' for Home anytime. Press 'Q' or ESC to Exit.", (40, 670), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (150, 150, 150), 1, cv2.LINE_AA)

        return canvas

    def repeat_instruction(self) -> None:
        entry = self.learning_db.get_entry(self.current_expected_mudra)
        if entry:
            self.audio_manager.speak(entry.voice_script, force=True)
        elif self.last_report and self.last_report.instructions:
            self.audio_manager.speak(self.last_report.instructions[0], force=True)

    def render_all_mudras_grid(self, canvas: np.ndarray) -> np.ndarray:
        """Renders an interactive 30-mudra selection grid overlay."""
        h, w = canvas.shape[:2]
        overlay = canvas.copy()
        cv2.rectangle(overlay, (0, 0), (w, h), (10, 10, 10), -1)
        cv2.addWeighted(overlay, 0.85, canvas, 0.15, 0, canvas)

        gx, gy, gw, gh = 80, 60, w - 160, h - 120
        cv2.rectangle(canvas, (gx, gy), (gx + gw, gy + gh), (25, 25, 25), -1)
        cv2.rectangle(canvas, (gx, gy), (gx + gw, gy + gh), (0, 165, 255), 2)

        cv2.putText(
            canvas,
            "ALL MUDRAS DIRECTORY — SELECT A LESSON (30 CLASSES)",
            (gx + 24, gy + 36),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.70,
            (255, 255, 255),
            2,
            cv2.LINE_AA,
        )

        close_text = "[ Click anywhere outside or press ESC to Close ]"
        cv2.putText(
            canvas,
            close_text,
            (gx + gw - 400, gy + 36),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            (0, 180, 255),
            1,
            cv2.LINE_AA,
        )

        cols = 6
        card_w, card_h = 164, 76
        spacing_x, spacing_y = 12, 12
        start_x, start_y = gx + 24, gy + 65

        for idx, mudra_id in enumerate(self.supported_mudras):
            r = idx // cols
            c = idx % cols
            cx = start_x + c * (card_w + spacing_x)
            cy = start_y + r * (card_h + spacing_y)

            is_active = (mudra_id == self.current_expected_mudra)
            border_col = (0, 220, 100) if is_active else (60, 60, 60)
            bg_col = (30, 45, 30) if is_active else (22, 22, 22)

            cv2.rectangle(canvas, (cx, cy), (cx + card_w, cy + card_h), bg_col, -1)
            cv2.rectangle(canvas, (cx, cy), (cx + card_w, cy + card_h), border_col, 2 if is_active else 1)

            entry = self.learning_db.get_entry(mudra_id)
            title = entry.traditional_name if entry else mudra_id
            if len(title) > 20:
                title = title[:19] + "…"

            cv2.putText(canvas, f"#{idx+1}", (cx + 8, cy + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.40, (0, 200, 255), 1, cv2.LINE_AA)
            cv2.putText(canvas, title, (cx + 8, cy + 44), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (255, 255, 255), 1, cv2.LINE_AA)
            status_lbl = "Pending Validation"
            cv2.putText(canvas, status_lbl, (cx + 8, cy + 63), cv2.FONT_HERSHEY_SIMPLEX, 0.32, (160, 160, 160), 1, cv2.LINE_AA)

        return canvas

    def toggle_evaluation_dashboard(self) -> bool:
        """Toggles the Evaluation Dashboard overlay open/closed without disrupting recognition."""
        opened = self.eval_dashboard.toggle()
        self.btn_eval.label = "Close Eval [E]" if opened else "Eval Dashboard [E]"
        self.btn_eval.active_color = (0, 220, 100) if opened else (0, 165, 255)
        return opened

    def open_evaluation_dashboard(self) -> None:
        self.eval_dashboard.open()
        self.btn_eval.label = "Close Eval [E]"
        self.btn_eval.active_color = (0, 220, 100)

    def close_evaluation_dashboard(self) -> None:
        self.eval_dashboard.close()
        self.btn_eval.label = "Eval Dashboard [E]"
        self.btn_eval.active_color = (0, 165, 255)

    def process_single_frame(
        self, frame: np.ndarray
    ) -> Tuple[MudraRecognitionResult, MudraCorrectionReport, Dict[str, float]]:
        """Processes a single frame, timing each phase accurately using time.perf_counter()."""
        t_proc_start = time.perf_counter()

        # 1. Inference latency
        t_rec_start = time.perf_counter()
        rec_result, _ = self.rec_engine.process_frame(
            frame,
            expected_mudra=self.current_expected_mudra,
        )
        inference_latency_ms = (time.perf_counter() - t_rec_start) * 1000.0

        # 2. Correction latency
        t_corr_start = time.perf_counter()
        report = self.corr_engine.evaluate_pose(
            rec_result,
            expected_mudra=self.current_expected_mudra,
        )
        correction_latency_ms = (time.perf_counter() - t_corr_start) * 1000.0

        # 3. Total pipeline latency
        total_processing_ms = (time.perf_counter() - t_proc_start) * 1000.0
        processing_throughput_fps = 1000.0 / max(1e-4, total_processing_ms)

        self.last_report = report

        perf_stats = {
            "inference_latency_ms": inference_latency_ms,
            "inference_ms": inference_latency_ms,
            "correction_latency_ms": correction_latency_ms,
            "correction_ms": correction_latency_ms,
            "total_processing_ms": total_processing_ms,
            "total_proc_ms": total_processing_ms,
            "camera_fps": float(self.fps),
            "processing_throughput_fps": processing_throughput_fps,
            "throughput_fps": processing_throughput_fps,
        }
        self.perf_metrics = perf_stats
        return rec_result, report, perf_stats

    def cycle_model(self) -> None:
        """Cycles active recognition model internally for admin/debug evaluation."""
        keys = [k for k in self.rec_engine.AVAILABLE_MODELS.keys() if k != "AUTO"]
        if self.current_model_key in keys:
            idx = keys.index(self.current_model_key)
            new_key = keys[(idx + 1) % len(keys)]
        else:
            new_key = "V1"
        self.current_model_key = new_key
        self.rec_engine.set_active_model(new_key)
        self.eval_dashboard.load_metrics(new_key)
        self.try_again()
        logger.info(f"Model cycled internally to: {self.rec_engine.AVAILABLE_MODELS[new_key]['name']}")

    def toggle_reference_skeleton(self) -> None:
        self.show_reference_skeleton = not self.show_reference_skeleton
        self.cached_ref_img = None
        logger.info(f"Reference skeleton overlay toggled: {self.show_reference_skeleton}")

    def handle_mouse_event(self, event: int, x: int, y: int, flags: int, param: Any) -> None:
        if event == cv2.EVENT_LBUTTONDOWN:
            # Check top tabs bar (y: 6..48)
            if 6 <= y <= 48:
                tabs_map = [
                    (15, 140, "HOME"),
                    (148, 273, "IDENTIFY"),
                    (281, 386, "LIBRARY"),
                    (394, 499, "PRACTICE"),
                    (507, 682, "CHALLENGE"),
                ]
                for x1, x2, t_name in tabs_map:
                    if x1 <= x <= x2:
                        self.set_tab(t_name)
                        return
                if self.screen_w - 135 <= x <= self.screen_w - 15:
                    self.set_tab("HOME")
                    return

            if self.current_tab == "HOME":
                card_w, card_h = 360, 110
                start_x, start_y = 40, 175
                cards_tabs = ["IDENTIFY", "LIBRARY", "CHALLENGE", "PRACTICE", "EVAL"]
                for idx, t_target in enumerate(cards_tabs):
                    r = idx // 3
                    c = idx % 3
                    if r == 1 and c == 2:
                        cx = start_x + 0 * (card_w + 25)
                        cy = start_y + 1 * (card_h + 18)
                    else:
                        cx = start_x + c * (card_w + 25)
                        cy = start_y + r * (card_h + 18)
                    if cx <= x <= cx + card_w and cy <= y <= cy + card_h:
                        self.set_tab(t_target)
                        return

                # Voice settings panel buttons on HOME screen (vx = 800, vy = 175, vw = 440, vh = 238)
                vx, vy = 800, 175
                # Test Voice button: vx + 16 .. vx + 205, vy + 138 .. vy + 178
                if vx + 16 <= x <= vx + 205 and vy + 138 <= y <= vy + 178:
                    self.test_voice()
                    return
                # Mute button: vx + 220 .. vx + 415, vy + 138 .. vy + 178
                if vx + 220 <= x <= vx + 415 and vy + 138 <= y <= vy + 178:
                    self.toggle_mute()
                    return

            if self.current_tab == "LIBRARY" or self.show_all_grid:
                gx, gy, gw, gh = 80, 60, self.screen_w - 160, self.screen_h - 120
                if gx <= x <= gx + gw and gy <= y <= gy + gh:
                    if gx + gw - 40 <= x <= gx + gw - 10 and gy + 10 <= y <= gy + 40:
                        self.show_all_grid = False
                        self.set_tab("HOME")
                        return
                    cols = 6
                    card_w, card_h = 164, 76
                    spacing_x, spacing_y = 12, 12
                    start_x, start_y = gx + 24, gy + 65
                    for idx, mudra_id in enumerate(self.supported_mudras):
                        r = idx // cols
                        c = idx % cols
                        cx = start_x + c * (card_w + spacing_x)
                        cy = start_y + r * (card_h + spacing_y)
                        if cx <= x <= cx + card_w and cy <= y <= cy + card_h:
                            self.select_mudra_by_id(mudra_id)
                            self.show_all_grid = False
                            self.set_tab("PRACTICE")
                            return
                return

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
        """Composes the professional dark studio interface layout with tab navigation."""
        if self.current_tab == "HOME":
            canvas = np.full((self.screen_h, self.screen_w, 3), 15, dtype=np.uint8)
            return self.render_home_screen(canvas)

        if self.current_tab == "EVAL" or self.eval_dashboard.is_open:
            canvas = self.eval_dashboard.render((self.screen_w, self.screen_h), perf_metrics=self.perf_metrics)
            self.render_top_tabs(canvas)
            for btn in self.buttons:
                btn.draw(canvas)
            return canvas

        if self.current_tab == "LIBRARY" or self.show_all_grid:
            canvas = np.full((self.screen_h, self.screen_w, 3), 15, dtype=np.uint8)
            canvas = self.render_all_mudras_grid(canvas)
            self.render_top_tabs(canvas)
            return canvas

        canvas = np.full((self.screen_h, self.screen_w, 3), 15, dtype=np.uint8)
        self.render_top_tabs(canvas)

        header_h = 58
        pane_y = header_h + 10
        pane_h = 415
        pane_w = (self.screen_w - 30) // 2

        left_x = 10
        annotated_cam = camera_frame.copy()
        h_cam, w_cam = annotated_cam.shape[:2]

        landmarks_to_draw = rec_result.hand_landmarks if rec_result.hand_landmarks is not None else rec_result.landmarks_21_3
        if rec_result.hand_detected and landmarks_to_draw is not None:
            annotated_cam = self.corr_engine.draw_correction_skeleton(
                annotated_cam,
                landmarks_to_draw,
                report.finger_statuses,
            )

        cam_resized = cv2.resize(annotated_cam, (pane_w, pane_h))
        canvas[pane_y : pane_y + pane_h, left_x : left_x + pane_w] = cam_resized
        cv2.rectangle(canvas, (left_x, pane_y), (left_x + pane_w, pane_y + pane_h), (60, 60, 60), 1)

        cv2.rectangle(canvas, (left_x, pane_y), (left_x + pane_w, pane_y + 30), (20, 20, 20), -1)
        hand_status_text = f"● Hand Detected ({rec_result.handedness})" if rec_result.hand_detected else "○ No Hand Detected"
        hand_status_color = (0, 220, 100) if rec_result.hand_detected else (0, 165, 255)

        cv2.putText(
            canvas,
            "LIVE CAM",
            (left_x + 12, pane_y + 20),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.48,
            (0, 220, 255),
            1,
            cv2.LINE_AA,
        )
        cv2.putText(
            canvas,
            hand_status_text,
            (left_x + 150, pane_y + 20),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.40,
            hand_status_color,
            1,
            cv2.LINE_AA,
        )

        if not rec_result.hand_detected:
            gx1, gy1 = left_x + pane_w // 2 - 140, pane_y + pane_h // 2 - 120
            gx2, gy2 = left_x + pane_w // 2 + 140, pane_y + pane_h // 2 + 120
            cv2.rectangle(canvas, (gx1, gy1), (gx2, gy2), (0, 165, 255), 2)
            cv2.putText(canvas, "PLACE ONE HAND INSIDE THE GUIDE", (gx1 + 12, gy1 + 30), cv2.FONT_HERSHEY_SIMPLEX, 0.40, (0, 220, 255), 1, cv2.LINE_AA)
            cv2.putText(canvas, "KEEP ALL FINGERS VISIBLE", (gx1 + 32, gy1 + 65), cv2.FONT_HERSHEY_SIMPLEX, 0.40, (200, 200, 200), 1, cv2.LINE_AA)
            cv2.putText(canvas, "KEEP PALM FACING CAMERA", (gx1 + 32, gy1 + 95), cv2.FONT_HERSHEY_SIMPLEX, 0.40, (200, 200, 200), 1, cv2.LINE_AA)

        if self.current_tab == "IDENTIFY":
            if rec_result.hand_detected and rec_result.predicted_mudra != "Uncertain":
                pred = rec_result.predicted_mudra
                if pred != getattr(self, "identify_last_stable_pred", ""):
                    self.identify_last_stable_pred = pred
                    self.identify_ref_start_time = time.monotonic()
                ref_mudra_to_show = self.identify_last_stable_pred
            else:
                ref_mudra_to_show = getattr(self, "identify_last_stable_pred", "1-Pataka")
        else:
            ref_mudra_to_show = self.current_expected_mudra

        right_x = left_x + pane_w + 10
        ref_visual = self.get_reference_visual(ref_mudra_to_show, (pane_w, pane_h))
        canvas[pane_y : pane_y + pane_h, right_x : right_x + pane_w] = ref_visual
        cv2.rectangle(canvas, (right_x, pane_y), (right_x + pane_w, pane_y + pane_h), (60, 60, 60), 1)

        status_y = pane_y + pane_h + 8
        status_h = self.screen_h - status_y - 58
        cv2.rectangle(canvas, (10, status_y), (self.screen_w - 10, status_y + status_h), (25, 25, 25), -1)
        cv2.rectangle(canvas, (10, status_y), (self.screen_w - 10, status_y + status_h), (50, 50, 50), 1)

        has_tolerances = report.has_instructor_tolerances
        is_match = rec_result.is_expected_match

        if not rec_result.hand_detected or rec_result.predicted_mudra == "Uncertain":
            status_title = "UNABLE TO ASSESS — POSITION YOUR HAND CLEARLY"
            status_col = (160, 160, 160)
        elif self.current_tab == "LIVE":
            status_title = f"LIVE RECOGNITION: {rec_result.predicted_mudra}"
            status_col = (0, 220, 100)
        elif not is_match:
            status_title = "WRONG MUDRA — PLEASE TRY AGAIN"
            status_col = (0, 0, 240)
        else:
            status_title = "MUDRA CLASS RECOGNIZED | POSE VALIDATION PENDING — REVIEW REQUIRED"
            status_col = (0, 165, 255)

        cv2.rectangle(canvas, (10, status_y), (18, status_y + status_h), status_col, -1)

        entry = self.learning_db.get_entry(self.current_expected_mudra)
        trad_name = entry.traditional_name if entry else self.current_expected_mudra

        cv2.putText(canvas, f"Target: {trad_name}", (25, status_y + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.48, (255, 255, 255), 1, cv2.LINE_AA)
        rec_disp = f"Predicted: {rec_result.predicted_mudra} ({rec_result.confidence*100:.1f}%)" if rec_result.hand_detected else "Recognized: Searching..."
        cv2.putText(canvas, rec_disp, (25, status_y + 42), cv2.FONT_HERSHEY_SIMPLEX, 0.48, (0, 220, 100) if is_match else (0, 140, 255), 1, cv2.LINE_AA)

        bar_x, bar_y, bar_w, bar_h = 25, status_y + 54, 200, 10
        cv2.rectangle(canvas, (bar_x, bar_y), (bar_x + bar_w, bar_y + bar_h), (40, 40, 40), -1)
        conf_fill = int(bar_w * min(1.0, max(0.0, rec_result.confidence)))
        cv2.rectangle(canvas, (bar_x, bar_y), (bar_x + conf_fill, bar_y + bar_h), (0, 200, 100), -1)
        cv2.rectangle(canvas, (bar_x, bar_y), (bar_x + bar_w, bar_y + bar_h), (80, 80, 80), 1)

        # Mirror the live result to the ESP32 bridge (published by a background
        # thread so a paused UI does not stop the panel updating).
        self._last_rec_result = rec_result

        cv2.putText(canvas, status_title, (245, status_y + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.44, status_col, 2, cv2.LINE_AA)
        if rec_result.hand_detected and rec_result.top3_predictions:
            top3_str = " | ".join([f"{p[0]}: {p[1]*100:.1f}%" for p in rec_result.top3_predictions[:3]])
            cv2.putText(canvas, f"Top-3: {top3_str}", (245, status_y + 44), cv2.FONT_HERSHEY_SIMPLEX, 0.36, (180, 180, 180), 1, cv2.LINE_AA)

        inst_text = report.instructions[0] if report.instructions else "Hold pose steadily within camera view."
        cv2.putText(canvas, f"Instruction: {inst_text[:95]}", (245, status_y + 64), cv2.FONT_HERSHEY_SIMPLEX, 0.38, (230, 230, 230), 1, cv2.LINE_AA)

        for btn in self.buttons:
            btn.draw(canvas)

        return canvas

    def run(self, max_frames: Optional[int] = None, save_sample_path: Optional[Path] = None) -> None:
        print("=" * 60)
        print("STARTING KUCHIPUDI MUDRA LEARNING STUDIO")
        print("Controls: 'n' (Next), 'p' (Prev), 't' (Try Again), 'r' (Repeat), 'm' (Mute), 's' (Skeleton), 'e' (Eval Dashboard), 'q' (Quit)")
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
                self.fps = 1.0 / max(1e-5, curr_time - prev_time)
                prev_time = curr_time

                # 1 & 2. Process frame with precise performance measurement
                rec_result, report, perf_stats = self.process_single_frame(frame)

                # 3. Event-Driven Audio Feedback
                if rec_result.hand_detected and not self.eval_dashboard.is_open:
                    lead_inst = report.instructions[0] if report.instructions else ""
                    self.audio_manager.notify_event(report.state, lead_inst)

                # 4. Render UI
                canvas = self.render_kiosk_frame(frame, rec_result, report, self.fps)

                # Safeguard: do NOT overwrite reports/kiosk_ui_fixed.png
                if save_sample_path:
                    p_str = str(save_sample_path).replace("\\", "/")
                    if p_str.endswith("kiosk_ui_fixed.png") and save_sample_path.exists():
                        logger.info("Preserving existing reports/kiosk_ui_fixed.png without overwriting.")
                    elif frames_count == 1:
                        save_sample_path.parent.mkdir(parents=True, exist_ok=True)
                        cv2.imwrite(str(save_sample_path), canvas)
                        logger.info(f"Saved kiosk UI screenshot to: {save_sample_path}")

                try:
                    cv2.imshow(self.window_name, canvas)
                    key = cv2.waitKey(30) & 0xFF
                    if key in (ord("q"), ord("Q")):
                        break
                    elif key == 27:  # ESC
                        if self.show_all_grid:
                            self.show_all_grid = False
                            self.set_tab("HOME")
                        elif self.eval_dashboard.is_open:
                            self.close_evaluation_dashboard()
                        else:
                            break
                    elif key in (ord("h"), ord("H")):
                        self.set_tab("HOME")
                    elif key in (ord("i"), ord("I")):
                        self.set_tab("IDENTIFY")
                    elif key in (ord("l"), ord("L")):
                        self.set_tab("LIBRARY")
                    elif key in (ord("p"), ord("P")):
                        self.set_tab("PRACTICE")
                    elif key in (ord("c"), ord("C")):
                        self.set_tab("CHALLENGE")
                    elif key in (ord("e"), ord("E")):
                        self.set_tab("EVAL")
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
                        p_str = str(save_sample_path).replace("\\", "/")
                        if not (p_str.endswith("kiosk_ui_fixed.png") and save_sample_path.exists()):
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


def publish_bridge_state(rec_result: Any) -> None:
    """Exports the live prediction so the ESP32 bridge can mirror it.

    The studio owns the camera, so src/esp32_bridge.py reads this file instead
    of running its own capture, and forwards the result to the TFT.
    """
    try:
        payload = {
            "mudra": str(getattr(rec_result, "predicted_mudra", "") or ""),
            "confidence": round(float(getattr(rec_result, "confidence", 0.0)) * 100.0, 1),
            "hand": bool(getattr(rec_result, "hand_detected", False)),
            "candidates": [
                {"mudra": str(p[0]), "confidence": round(float(p[1]) * 100.0, 1)}
                for p in (getattr(rec_result, "top3_predictions", None) or [])[:3]
            ],
            "ts": time.time(),
        }
        target = Path(tempfile.gettempdir()) / "mudra_bridge_state.json"
        tmp = target.with_suffix(".tmp")
        tmp.write_text(json.dumps(payload), encoding="utf-8")
        tmp.replace(target)
    except Exception:
        pass


def _acquire_single_instance(port: int = 50505):
    """Refuses to start a second studio: two instances fight over the camera."""
    import socket

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind(("127.0.0.1", port))
        sock.listen(1)
    except OSError:
        print("Another Mudra Learning Studio is already running; exiting.")
        raise SystemExit(0)
    return sock  # caller must keep the reference alive


def main():
    parser = argparse.ArgumentParser(description="Kuchipudi Mudra Learning Studio Kiosk")
    parser.add_argument("--camera", type=int, default=0, help="Camera index")
    parser.add_argument("--model", type=str, default="AUTO", choices=["AUTO", "V1", "V2", "V3", "V4", "V5"], help="Default model (AUTO default)")
    parser.add_argument("--max-frames", type=int, default=None, help="Max frames for test mode")
    parser.add_argument("--save-sample", type=Path, default=None, help="Path to save UI screenshot")
    parser.add_argument("--eval-dashboard", action="store_true", help="Start directly in Evaluation Dashboard mode")
    args = parser.parse_args()

    instance_lock = _acquire_single_instance()  # noqa: F841 - kept alive for the process lifetime
    kiosk = InteractiveMudraKiosk(camera_index=args.camera, default_model=args.model)
    if args.eval_dashboard:
        kiosk.open_evaluation_dashboard()
    kiosk.run(max_frames=args.max_frames, save_sample_path=args.save_sample)


if __name__ == "__main__":
    main()
