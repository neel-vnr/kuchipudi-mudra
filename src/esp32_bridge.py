"""ESP32 <-> laptop bridge for the MUDRA AI display.

Responsibilities
----------------
1. Find the ESP32 automatically: scan every serial port, read the device banner
   ("# boot MUDRA AI ..." or the {"event":"device"} JSON), and keep that port.
   If the board is unplugged it waits and reconnects.
2. Listen for the commands the device sends when the user taps the screen:
      START_MODE:learn | practice | challenge | exam
      STOP_DETECTION
      REFERENCE:<mudra>
      GET_STATUS
3. Open a camera - an external webcam when one is present, otherwise the
   laptop's built-in camera - and run the existing recognition engine.
4. Publish results as newline-delimited JSON with the top three candidates, a
   confidence score and a short suggestion.

The display never runs the model; it only renders what this process sends.

Usage
-----
    python src/esp32_bridge.py                 # auto-detect port, wait if absent
    python src/esp32_bridge.py --port COM8
    python src/esp32_bridge.py --list-cameras
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import threading
import time
from typing import Dict, List, Optional, Tuple

import cv2
import numpy as np

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - dependency hint
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    raise

# Allow running from the repo root or from src/.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recognition_engine import MudraRecognitionEngine  # noqa: E402

BAUD = 921600
IMAGE_W, IMAGE_H = 240, 180
# Bytes of RGB565 sent per line. 240 bytes -> 480 hex chars, comfortably inside
# the device's line buffer.
CHUNK_BYTES = 240
REFERENCE_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data",
    "kuchipudi_references",
    "images",
)

# How often a detection sample is pushed to the display.
SEND_INTERVAL_S = 0.15
# Confidence below what the engine calls "recognised" gets a coaching suggestion.
COACHING_THRESHOLD = 0.60

# The studio (kiosk_app.py) owns the camera and publishes its live prediction
# here; the bridge reads it and mirrors it onto the TFT.
KIOSK_STATE = os.path.join(tempfile.gettempdir(), "mudra_bridge_state.json")
KIOSK_STALE_S = 1.5

# One window for both the live viewfinder and the idle placard, so it does not
# jump around when a session starts or stops.
WINDOW_TITLE = "MUDRA AI - camera"


# ---------------------------------------------------------------------------
# Reference library
# ---------------------------------------------------------------------------
def load_reference_images() -> Dict[str, str]:
    """Maps a display mudra name to its reference image path."""
    images: Dict[str, str] = {}
    for path in sorted(glob.glob(os.path.join(REFERENCE_DIR, "*_reference.*"))):
        stem = os.path.splitext(os.path.basename(path))[0]
        stem = stem.replace("_reference", "")
        stem = re.sub(r"^\d+-", "", stem)  # drop the leading "12-"
        images[stem] = path
    return images


def humanize(class_name: str) -> str:
    """'12-Katakamukha_1' -> 'Katakamukha 1'."""
    name = re.sub(r"^\d+-", "", class_name).replace("_", " ")
    return name


def find_reference(images: Dict[str, str], mudra: str) -> Optional[str]:
    key = re.sub(r"[^a-z0-9]", "", mudra.lower())
    for name, path in images.items():
        if re.sub(r"[^a-z0-9]", "", name.lower()) == key:
            return path
    return None


# ---------------------------------------------------------------------------
# Encoded-image cache
# ---------------------------------------------------------------------------
_encoded: Dict[str, bytes] = {}
_encoding: set = set()
_encoded_lock = threading.Lock()


def encode_rgb565(path: str) -> bytes:
    """Loads a reference image and returns 240x180 RGB565 little-endian bytes."""
    image = cv2.imread(path)
    if image is None:
        raise RuntimeError(f"cannot read {path}")
    image = cv2.resize(image, (IMAGE_W, IMAGE_H), interpolation=cv2.INTER_AREA)
    bgr = image.astype(np.uint16)
    r = ((bgr[:, :, 2] >> 3) << 11).astype(np.uint16)
    g = ((bgr[:, :, 1] >> 2) << 5).astype(np.uint16)
    b = (bgr[:, :, 0] >> 3).astype(np.uint16)
    return (r | g | b).astype("<u2").tobytes()


def preload(mudra: str, path: str) -> None:
    """Encodes a reference in the background so VIEW MUDRA does not wait."""
    with _encoded_lock:
        if mudra in _encoded or mudra in _encoding:
            return
        _encoding.add(mudra)

    def worker() -> None:
        try:
            data = encode_rgb565(path)
            with _encoded_lock:
                _encoded[mudra] = data
            print(f"[image] pre-encoded {mudra} ({len(data) // 1024} KB)")
        except Exception as exc:  # keep the bridge alive
            print(f"[image] preload failed for {mudra}: {exc}")
        finally:
            with _encoded_lock:
                _encoding.discard(mudra)

    threading.Thread(target=worker, daemon=True).start()


def get_encoded(mudra: str, path: str) -> bytes:
    with _encoded_lock:
        data = _encoded.get(mudra)
    return data if data is not None else encode_rgb565(path)


# ---------------------------------------------------------------------------
# Camera selection
# ---------------------------------------------------------------------------
def list_cameras(max_index: int = 6) -> List[int]:
    found: List[int] = []
    for index in range(max_index):
        cap = cv2.VideoCapture(index, cv2.CAP_DSHOW if os.name == "nt" else cv2.CAP_ANY)
        if cap.isOpened():
            ok, _ = cap.read()
            if ok:
                found.append(index)
        cap.release()
    return found


def open_camera(prefer_external: bool = True) -> Tuple[cv2.VideoCapture, int]:
    """Opens an external webcam when present, else the built-in camera.

    On Windows the built-in camera is normally index 0 and an external USB
    webcam appears as index 1 or higher, so the highest working index is chosen
    when the caller wants the external device.
    """
    available = list_cameras()
    if not available:
        raise RuntimeError("no camera found")

    order = sorted(available, reverse=prefer_external)  # highest index first
    for index in order:
        cap = cv2.VideoCapture(index, cv2.CAP_DSHOW if os.name == "nt" else cv2.CAP_ANY)
        if cap.isOpened():
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            return cap, index
        cap.release()
    raise RuntimeError("camera opened but produced no frames")


# ---------------------------------------------------------------------------
# Serial link
# ---------------------------------------------------------------------------
class DeviceLink:
    """Owns the serial port and reconnects when the board is re-plugged."""

    def __init__(self, port: Optional[str]) -> None:
        self.preferred = port
        self.ser: Optional[serial.Serial] = None
        self._buffer = b""

    def connected(self) -> bool:
        return self.ser is not None and self.ser.is_open

    def _candidate_ports(self) -> List[str]:
        if self.preferred:
            return [self.preferred]
        return [p.device for p in list_ports.comports()]

    def connect(self, timeout_s: float = 3.0) -> bool:
        for port in self._candidate_ports():
            try:
                ser = serial.Serial()
                ser.port = port
                ser.baudrate = BAUD
                ser.timeout = 0
                # Never reset the board just by attaching.
                ser.dtr = False
                ser.rts = False
                ser.open()
            except Exception:
                continue

            deadline = time.time() + timeout_s
            seen = b""
            probed = False
            while time.time() < deadline:
                # Probe actively: the device answers PING with a JSON pong, and
                # HELLO with its device identity. That works whether the board
                # booted five seconds or five hours ago.
                if not probed:
                    try:
                        ser.write(b"PING\nHELLO\n")
                    except Exception:
                        pass
                    probed = True

                chunk = ser.read(256)
                if chunk:
                    seen += chunk
                    if (b"MUDRA AI" in seen or b'"event":"device"' in seen
                            or b'"event":"pong"' in seen):
                        self.ser = ser
                        self._buffer = b""
                        print(f"[link] ESP32 found on {port}")
                        return True
            ser.close()
        return False

    def read_lines(self) -> List[str]:
        if not self.connected():
            return []
        try:
            self._buffer += self.ser.read(4096)
        except Exception:
            self.close()
            return []

        lines: List[str] = []
        while b"\n" in self._buffer:
            raw, self._buffer = self._buffer.split(b"\n", 1)
            text = raw.decode("utf-8", "replace").strip()
            if text:
                lines.append(text)
        # Guard against an endless partial line.
        if len(self._buffer) > 4096:
            self._buffer = b""
        return lines

    def send(self, payload: Dict) -> None:
        if not self.connected():
            return
        try:
            self.ser.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
        except Exception:
            self.close()

    def send_raw(self, text: str) -> None:
        """Writes an already-formatted line (used for the image stream)."""
        if not self.connected():
            return
        try:
            self.ser.write(text.encode())
        except Exception:
            self.close()

    def send_bytes(self, data: bytes) -> None:
        """Writes raw bytes immediately (RGB565 image payload)."""
        if not self.connected():
            return
        try:
            self.ser.write(data)
            self.ser.flush()
        except Exception:
            self.close()

    def close(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None


# ---------------------------------------------------------------------------
# Session
# ---------------------------------------------------------------------------
class Session:
    """One learn / practice / challenge / exam run."""

    def __init__(self, mode: str, engine: MudraRecognitionEngine, images: Dict[str, str]) -> None:
        self.mode = mode
        self.engine = engine
        self.images = images
        self.started_at = time.time()
        self.attempts: List[Tuple[str, float]] = []
        self.expected = ""
        self._setup_expected()

    def _setup_expected(self) -> None:
        if self.mode == "challenge" and self.images:
            self.expected = humanize(random.choice(list(self.images.keys())))
        elif self.mode == "exam" and self.images:
            self.expected = humanize(random.choice(list(self.images.keys())))

    def suggestion(self, mudra: str, confidence: float) -> str:
        if not mudra:
            return "Show your hand clearly to the camera."
        if confidence >= COACHING_THRESHOLD:
            return f"Good match for {humanize(mudra)}."
        if self.expected:
            return f"Aim for {self.expected}. Keep the palm steady."
        return "Adjust your hand and hold it steady."

    def summary(self) -> Dict:
        total = len(self.attempts)
        hits = sum(1 for _, c in self.attempts if c >= COACHING_THRESHOLD)
        return {
            "event": "session",
            "mode": self.mode,
            "attempts": total,
            "matches": hits,
            "seconds": round(time.time() - self.started_at, 1),
        }


# ---------------------------------------------------------------------------
# Main bridge
# ---------------------------------------------------------------------------
class Bridge:
    def __init__(self, port: Optional[str], preview: bool, kiosk: bool = False) -> None:
        self.link = DeviceLink(port)
        self.preview = preview
        # The bridge detects by default. The studio is only started on request
        # from the panel (KIOSK:START), so the two never fight over the camera.
        self.kiosk_mode = kiosk
        self.kiosk: Optional[subprocess.Popen] = None
        self.images = load_reference_images()
        self.engine: Optional[MudraRecognitionEngine] = None
        self.session: Optional[Session] = None
        self.capture: Optional[cv2.VideoCapture] = None
        self.camera_index = -1
        self._last_send = 0.0
        self._warmed = False
        self._last_heartbeat = 0.0
        self._last_idle_frame = 0.0
        self.camera_label = ""

    # -- lifecycle ---------------------------------------------------------
    def on_device_connected(self) -> None:
        """Called the moment the ESP32 answers on a COM port.

        Warms the model, the reference cache and the camera, then raises the
        laptop UI window straight away so the screen is live before the learner
        touches the panel.
        """
        self.link.send({"event": "connection", "status": "connected"})
        if not self._warmed:
            self.warm_up()
            self._warmed = True
        if self.kiosk_mode:
            self.launch_kiosk()
        else:
            self.show_idle_window("ESP32 connected", "Choose a task on the display")

    def launch_kiosk(self) -> None:
        """Starts the full Mudra Learning Studio UI on the laptop."""
        if self.kiosk is not None and self.kiosk.poll() is None:
            return
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kiosk_app.py")
        if not os.path.isfile(script):
            print(f"[kiosk] not found: {script}")
            return
        print(f"[kiosk] launching {script}")
        try:
            self.kiosk = subprocess.Popen([sys.executable, script])
        except Exception as exc:
            print(f"[kiosk] launch failed: {exc}")
    def relay_kiosk_state(self) -> None:
        """Forwards the studio's live prediction to the display."""
        now = time.time()
        if now - self._last_send < SEND_INTERVAL_S:
            return
        try:
            with open(KIOSK_STATE, "r", encoding="utf-8") as fh:
                payload = json.load(fh)
        except Exception:
            return
        if now - float(payload.get("ts", 0.0)) > KIOSK_STALE_S:
            return  # studio is not running / not updating

        self._last_send = now
        hand = bool(payload.get("hand", False))
        raw_name = str(payload.get("mudra", "") or "")
        name = humanize(raw_name) if raw_name else ""
        confidence = float(payload.get("confidence", 0.0))

        if not hand or not name or raw_name.lower() == "uncertain":
            self.link.send({"event": "detection", "status": "no_hand", "mudra": "", "confidence": 0})
            return

        path = find_reference(self.images, name)
        if path is not None:
            preload(name, path)

        suggestion = (f"Good match for {name}."
                      if confidence >= COACHING_THRESHOLD * 100.0
                      else "Adjust your hand and hold it steady.")
        ranked = [
            {"mudra": humanize(str(c.get("mudra", ""))), "confidence": float(c.get("confidence", 0.0))}
            for c in (payload.get("candidates") or [])
            if c.get("mudra")
        ][:3]

        self.link.send({
            "event": "detection",
            "status": "recognized" if confidence >= COACHING_THRESHOLD * 100.0 else "low_confidence",
            "mudra": name,
            "confidence": round(confidence, 1),
            "suggestion": suggestion,
            "candidates": ranked,
        })

    def show_idle_window(self, headline: str, detail: str = "") -> None:
        if not self.preview:
            return
        canvas = np.zeros((360, 640, 3), dtype=np.uint8)
        cv2.putText(canvas, "MUDRA AI", (28, 70), cv2.FONT_HERSHEY_SIMPLEX, 1.6, (0, 220, 255), 3,
                    cv2.LINE_AA)
        cv2.putText(canvas, headline, (28, 170), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (240, 240, 240), 2,
                    cv2.LINE_AA)
        if detail:
            cv2.putText(canvas, detail, (28, 215), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (150, 150, 150), 2,
                        cv2.LINE_AA)
        cv2.imshow(WINDOW_TITLE, canvas)
        cv2.waitKey(1)

    def ensure_engine(self) -> MudraRecognitionEngine:
        if self.engine is None:
            print("[model] loading V5 only ...")
            # V1-V4 are not needed by the bridge; skipping them saves several
            # seconds of start-up and a lot of memory.
            self.engine = MudraRecognitionEngine(default_model="V5", warm_load=["V5"])
        return self.engine

    def open_camera(self) -> None:
        if self.capture is not None:
            return
        self.capture, self.camera_index = open_camera(prefer_external=True)
        self.camera_label = "external" if self.camera_index > 0 else "built-in"
        print(f"[camera] using index {self.camera_index} ({self.camera_label})")
        self.link.send({"event": "camera", "status": "active"})
        # The viewfinder opens straight away, not when a task is picked.
        self.idle_viewfinder(force=True)

    def idle_viewfinder(self, force: bool = False) -> None:
        """Live preview whenever a camera is active, session or not."""
        if not self.preview or self.capture is None:
            return
        now = time.time()
        if not force and (now - self._last_idle_frame) < 0.05:  # cap at ~20 fps
            return
        self._last_idle_frame = now

        ok, frame = self.capture.read()
        if not ok or frame is None:
            return
        cv2.putText(frame, f"CAMERA ACTIVE  ({self.camera_label})", (14, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 220, 255), 2, cv2.LINE_AA)
        cv2.putText(frame, "choose a task on the display", (14, 58),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (230, 230, 230), 1, cv2.LINE_AA)
        cv2.imshow(WINDOW_TITLE, frame)
        cv2.waitKey(1)

    def close_camera(self) -> None:
        if self.capture is not None:
            self.capture.release()
            self.capture = None
        self.link.send({"event": "camera", "status": "inactive"})

    # -- commands from the device -----------------------------------------
    def handle_line(self, line: str) -> None:
        if line.startswith("#"):
            return  # human-readable device log
        if line.startswith("{"):
            return  # device JSON event, nothing to do here

        if line.startswith("START_MODE:"):
            mode = line.split(":", 1)[1].strip().lower()
            self.prepare_session(mode)
        elif line in ("BEGIN_SESSION",):
            self.begin_session()
        elif line in ("STOP_DETECTION", "CMD:STOP"):
            self.stop_session()
        elif line.startswith("REFERENCE:"):
            self.send_reference(line.split(":", 1)[1].strip())
        elif line in ("GET_STATUS", "CMD:STATUS"):
            self.publish_status()
        elif line in ("PING",):
            self.link.send({"event": "pong", "status": "ok"})
        elif line in ("KIOSK:START", "STUDIO:START"):
            self.enter_kiosk_mode()

    def enter_kiosk_mode(self) -> None:
        """Hands the camera to the studio, on request from the panel.

        The bridge releases the camera first so the studio can open it, then
        keeps mirroring the studio's results back to the display.
        """
        if self.kiosk_mode:
            self.launch_kiosk()
            return
        print("[kiosk] requested from the panel: releasing camera")
        self.stop_session()
        self.close_camera()
        time.sleep(0.8)  # let Windows release the capture device
        self.kiosk_mode = True
        self.launch_kiosk()

    # -- target preparation -------------------------------------------------
    def prepare_session(self, mode: str) -> None:
        """Picks the target for this task and sends it to the display.

        learn     -> reference picture + name (picture shown on both screens)
        challenge -> the mudra name, as the challenge
        exam      -> the mudra name to perform
        practice  -> no target
        """
        print(f"[session] preparing '{mode}'")
        engine = self.ensure_engine() if not self.kiosk_mode else self.engine  # load the model now, not on START

        if mode in ("detect", "practice") or not self.images:
            self.link.send({"event": "target", "mode": mode, "mudra": "", "image": False})
            self.session = Session(mode, engine, self.images)
            return

        mudra = humanize(random.choice(list(self.images.keys())))
        path = find_reference(self.images, mudra)
        wants_image = mode == "learn" and path is not None

        print(f"[session] target = {mudra}" + (" (with picture)" if wants_image else ""))
        self.link.send({"event": "target", "mode": mode, "mudra": mudra, "image": wants_image})

        if wants_image:
            self.send_image(path, mudra)
        else:
            self.show_target_window(mudra, path, mode)

        self.session = Session(mode, engine, self.images)
        self.session.expected = mudra

    def show_target_window(self, mudra: str, path: Optional[str], mode: str) -> None:
        """Shows the challenge/exam target in the laptop UI as well."""
        canvas = np.zeros((240, 480, 3), dtype=np.uint8)
        cv2.putText(canvas, f"{mode.upper()}: {mudra}", (16, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.9,
                    (255, 255, 255), 2, cv2.LINE_AA)
        if path is not None:
            ref = cv2.imread(path)
            if ref is not None:
                ref = cv2.resize(ref, (200, 200))
                canvas[30:230, 260:460] = ref
        cv2.imshow("MUDRA AI - target", canvas)
        cv2.waitKey(1)

    def send_image(self, path: str, mudra: str) -> None:
        """Streams the reference to the panel as raw RGB565 (fastest path)."""
        try:
            data = get_encoded(mudra, path)
        except Exception as exc:
            print(f"[image] {exc}")
            return

        print(f"[image] sending {IMAGE_W}x{IMAGE_H} RGB565 ({len(data) // 1024} KB) ...")
        started = time.time()
        self.link.send_raw(f"IMGBIN:{IMAGE_W},{IMAGE_H}\n")
        self.link.send_bytes(data)
        print(f"[image] sent in {time.time() - started:.1f}s")

        # Same picture in the laptop UI.
        preview = cv2.imread(path)
        if preview is not None:
            preview = cv2.resize(preview, (360, 270))
            cv2.putText(preview, mudra, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2,
                        cv2.LINE_AA)
            cv2.imshow(f"MUDRA AI - {mudra}", preview)
            cv2.waitKey(1)

    def begin_session(self) -> None:
        if self.session is None:
            self.session = Session("detect", self.ensure_engine(), self.images)
        print(f"[session] camera starting ({self.session.mode})")

        if not self.kiosk_mode:
            try:
                self.open_camera()
            except Exception as exc:
                print(f"[session] camera unavailable: {exc}")
                self.link.send({"event": "camera", "status": "error"})
                self.link.send({"event": "detection", "status": "camera_disconnected",
                                "mudra": "", "confidence": 0})
                return

        if self.session.engine is not None:
            self.session.engine.clear_smoothing()
        self.link.send({"event": "model", "status": "ready"})
        self.link.send({"event": "ui", "action": "detection"})

    def stop_session(self) -> None:
        if self.session is not None:
            print(f"[session] stopped: {self.session.summary()}")
            self.link.send(self.session.summary())
        self.session = None
        self.close_camera()
        self.show_idle_window("Session ended", "Choose a task on the display")

    def send_reference(self, mudra: str) -> None:
        """Shows the reference locally and streams it to the panel."""
        path = find_reference(self.images, mudra)
        if path is None:
            print(f"[reference] no image for '{mudra}'")
            self.link.send({"event": "reference", "status": "missing", "mudra": mudra})
            return

        print(f"[reference] {mudra} -> {path}")
        self.send_image(path, humanize(mudra))
        self.link.send({"event": "reference", "status": "shown", "mudra": humanize(mudra)})

    def publish_status(self) -> None:
        self.link.send({"event": "status", "status": "detecting" if self.session else "idle"})

    # -- recognition loop --------------------------------------------------
    def _heartbeat(self, status: str = "idle") -> None:
        """Keeps the device's 5 s link watchdog fed when no results are flowing."""
        now = time.time()
        if now - self._last_heartbeat < 1.5:
            return
        self._last_heartbeat = now
        self.link.send({"event": "status", "status": status})

    def step(self) -> None:
        if self.kiosk_mode:
            self.relay_kiosk_state()
            self._heartbeat("detecting")
            time.sleep(0.02)
            return

        if self.session is None or self.capture is None:
            self._heartbeat("idle")
            self.idle_viewfinder()
            if self.preview and self.capture is None:
                cv2.waitKey(1)  # keeps the placard window responsive
            time.sleep(0.02)
            return

        ok, frame = self.capture.read()
        if not ok or frame is None:
            time.sleep(0.05)
            return

        result, display = self.session.engine.process_frame(frame, expected_mudra=self.session.expected)

        now = time.time()
        if now - self._last_send < SEND_INTERVAL_S:
            if self.preview:
                cv2.imshow(WINDOW_TITLE, display)
                cv2.waitKey(1)
            return
        self._last_send = now

        if not result.hand_detected:
            self.link.send({"event": "detection", "status": "no_hand", "mudra": "", "confidence": 0})
        else:
            candidates = [
                {"mudra": humanize(p.class_name), "confidence": round(p.probability * 100.0, 1)}
                for p in (result.top3_predictions or [])[:3]
            ]
            top_name = humanize(result.top1_mudra) if result.top1_mudra else ""
            confidence = float(result.top1_confidence) * 100.0

            # Encode the reference in the background as soon as the name is
            # known, so VIEW MUDRA just has to stream it.
            if top_name:
                reference_path = find_reference(self.images, top_name)
                if reference_path is not None:
                    preload(top_name, reference_path)
            status = "recognized" if confidence >= COACHING_THRESHOLD * 100.0 else "low_confidence"
            if self.session.mode in ("challenge", "exam") and self.session.expected:
                status = "recognized" if top_name == self.session.expected else "low_confidence"
            self.session.attempts.append((top_name, confidence / 100.0))

            self.link.send({
                "event": "detection",
                "status": status,
                "mudra": top_name,
                "confidence": round(confidence, 1),
                "suggestion": self.session.suggestion(top_name, confidence / 100.0),
                "candidates": candidates,
            })

        if self.preview:
            cv2.imshow(WINDOW_TITLE, display)
            cv2.waitKey(1)

    # -- main loop ---------------------------------------------------------
    def warm_up(self) -> None:
        """Loads the model, caches every reference and opens the camera up front.

        Everything the user can trigger from the panel is then already resident,
        so selecting a task costs a screen transition instead of several seconds
        of loading.
        """
        print(f"[warm] encoding {len(self.images)} reference images ...")
        for name, path in self.images.items():
            preload(name, path)
        deadline = time.time() + 10.0
        while time.time() < deadline:
            with _encoded_lock:
                if len(_encoded) >= len(self.images):
                    break
            time.sleep(0.05)
        with _encoded_lock:
            cached = len(_encoded)
        print(f"[warm] {cached}/{len(self.images)} references cached")

        if self.kiosk_mode:
            print("[warm] studio mode: camera and model belong to kiosk_app.py")
            return

        print("[warm] loading V5 model ...")
        engine = self.ensure_engine()

        try:
            self.open_camera()
        except Exception as exc:
            print(f"[warm] camera not ready yet ({exc}); will retry on START")

        # The first inference initialises the MediaPipe graphs; do it now.
        try:
            blank = np.zeros((480, 640, 3), dtype=np.uint8)
            engine.process_frame(blank)
            print("[warm] inference graph ready")
        except Exception as exc:
            print(f"[warm] warm-up inference failed: {exc}")

    def run(self) -> None:
        print(f"[library] {len(self.images)} reference images")
        while True:
            if not self.link.connected():
                if not self.link.connect():
                    if self._warmed:
                        # Board unplugged: keep the UI alive and say so.
                        self.show_idle_window("Waiting for ESP32", "Plug the board in ...")
                        self._warmed = False
                    time.sleep(1.0)
                    continue
                self.on_device_connected()

            for line in self.link.read_lines():
                self.handle_line(line)
            self.step()


def main() -> int:
    parser = argparse.ArgumentParser(description="MUDRA AI ESP32 bridge")
    parser.add_argument("--port", help="serial port, e.g. COM8 (default: auto-detect)")
    parser.add_argument("--preview", dest="preview", action="store_true",
                        help="show the live camera feed in a laptop window (default)")
    parser.add_argument("--no-preview", dest="preview", action="store_false",
                        help="disable the camera window")
    parser.set_defaults(preview=True)
    parser.add_argument("--list-cameras", action="store_true", help="print camera indexes and exit")
    parser.add_argument("--kiosk", dest="kiosk", action="store_true",
                        help="launch kiosk_app.py immediately instead of waiting for the panel")
    parser.set_defaults(kiosk=False)
    args = parser.parse_args()

    if args.list_cameras:
        print("cameras:", list_cameras())
        return 0

    Bridge(args.port, args.preview, kiosk=args.kiosk).run()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n[bridge] stopped")



