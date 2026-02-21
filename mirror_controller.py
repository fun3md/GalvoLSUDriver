# mirror_controller.py
"""
Python host application for resonant mirror controller (ESP32).
Arduino sketch expects newline-delimited JSON with this shape:

Commands:
  {"cmd":"arm","value":true/false}
  {"cmd":"status"}
  {"cmd":"get","path":"*"}  -> telemetry
  {"cmd":"set","path":"ttl.pixelWidth_us","value":1}
  {"cmd":"set","path":"ttl.extraOffset_us","value":0}
  {"cmd":"set","path":"ttl.ttlFreq_hz","value":1000000}
  {"cmd":"set","path":"dots.testPatternEnable","value":true/false}
  {"cmd":"set","path":"dots.testCount","value":100}
  {"cmd":"dots.inactive","dots":[{"idxNorm":0,"rgbMask":1}, ...]}
  {"cmd":"dots.swap","value":true}

This file provides:
- Serial transport with reader thread and response queue
- Gradio web UI for connect/arm/status/telemetry
- Dot editor: generate simple patterns, upload inactive, request swap

Install:
  pip install pyserial gradio

Run:
  python mirror_controller.py
"""

from __future__ import annotations

import json
import time
import threading
import queue
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import serial
import serial.tools.list_ports
import gradio as gr


BAUD_RATE = 115200
READ_TIMEOUT_S = 0.2
DOT_CAP = 100


# ------------------------- Serial client -------------------------

@dataclass
class DeviceLine:
    t: float
    raw: str
    obj: Optional[Dict[str, Any]]


class MirrorSerialClient:
    """
    Line-based JSON serial client.
    - Starts a background thread reading lines and parsing JSON.
    - send_json() writes a line and optionally waits for the next JSON response line.
      (Your firmware does not echo seq, so we treat the "next JSON line" as response.)
    """
    def __init__(self) -> None:
        self.ser: Optional[serial.Serial] = None
        self._rx_thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._rxq: "queue.Queue[DeviceLine]" = queue.Queue(maxsize=1000)
        self._lock = threading.Lock()

    @staticmethod
    def list_ports() -> List[Tuple[str, str]]:
        ports = []
        for p in serial.tools.list_ports.comports():
            label = f"{p.device} — {p.description}"
            ports.append((p.device, label))
        return ports

    @staticmethod
    def autodetect_port() -> str:
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            desc = (p.description or "").lower()
            hwid = (p.hwid or "").lower()
            if "usb" in desc or "cp210" in desc or "ch340" in desc or "uart" in desc or "serial" in desc:
                return p.device
        return ports[0].device if ports else ""

    def is_open(self) -> bool:
        return bool(self.ser and self.ser.is_open)

    def open(self, port: str) -> None:
        self.close()
        self._stop.clear()
        self.ser = serial.Serial(port, BAUD_RATE, timeout=READ_TIMEOUT_S, write_timeout=1.0)
        # give ESP32 time to reboot on DTR toggle
        time.sleep(1.8)
        self._rx_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._rx_thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._rx_thread and self._rx_thread.is_alive():
            self._rx_thread.join(timeout=0.5)
        self._rx_thread = None
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self._drain_rx()

    def _drain_rx(self) -> None:
        try:
            while True:
                self._rxq.get_nowait()
        except queue.Empty:
            pass

    def _reader_loop(self) -> None:
        assert self.ser
        while not self._stop.is_set():
            try:
                line = self.ser.readline()
            except Exception:
                break
            if not line:
                continue
            raw = line.decode("utf-8", errors="replace").strip()
            obj = None
            if raw.startswith("{") and raw.endswith("}"):
                try:
                    obj = json.loads(raw)
                except Exception:
                    obj = None
            dl = DeviceLine(t=time.time(), raw=raw, obj=obj)
            try:
                self._rxq.put_nowait(dl)
            except queue.Full:
                # drop oldest
                try:
                    _ = self._rxq.get_nowait()
                except queue.Empty:
                    pass
                try:
                    self._rxq.put_nowait(dl)
                except queue.Full:
                    pass

    def send_json(self, payload: Dict[str, Any], wait_json: bool = True, timeout_s: float = 1.0) -> DeviceLine:
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial not open")

        msg = json.dumps(payload, separators=(",", ":")) + "\n"
        with self._lock:
            self.ser.write(msg.encode("utf-8"))
            self.ser.flush()

        if not wait_json:
            return DeviceLine(t=time.time(), raw="(sent)", obj=None)

        deadline = time.time() + timeout_s
        while time.time() < deadline:
            try:
                dl = self._rxq.get(timeout=0.05)
            except queue.Empty:
                continue
            if dl.obj is not None:
                return dl
            # if it's not JSON, keep waiting but we still want it for logs
        return DeviceLine(t=time.time(), raw="(timeout waiting for JSON)", obj={"error": "timeout"})

    def get_recent_lines(self, max_lines: int = 200) -> List[DeviceLine]:
        # Non-destructive snapshot: pull all then push back
        items: List[DeviceLine] = []
        try:
            while True:
                items.append(self._rxq.get_nowait())
        except queue.Empty:
            pass
        for it in items:
            try:
                self._rxq.put_nowait(it)
            except queue.Full:
                break
        return items[-max_lines:]


client = MirrorSerialClient()


# Add these imports near the top of mirror_controller.py
from PIL import Image
import numpy as np

# ------------------------- Image -> dots -------------------------

def _img_to_rgb_array(path: str) -> np.ndarray:
    """
    Load image from disk and return RGB uint8 array shape (H,W,3).
    """
    im = Image.open(path).convert("RGB")
    return np.array(im, dtype=np.uint8)

def _scale_image_for_dots(rgb: np.ndarray, dot_cap: int) -> np.ndarray:
    """
    Scale image to width=dot_cap (so one row maps to DOT_CAP dot positions).
    Keeps height as-is proportionally (so you can scan multiple lines).
    """
    h, w, _ = rgb.shape
    if w == dot_cap:
        return rgb
    im = Image.fromarray(rgb, mode="RGB")
    new_w = dot_cap
    new_h = max(1, int(round(h * (new_w / float(w)))))
    im2 = im.resize((new_w, new_h), resample=Image.BILINEAR)
    return np.array(im2, dtype=np.uint8)

def image_line_to_dots(
    image_path: str,
    line_index: Optional[int] = None,
    dot_cap: int = DOT_CAP,
    threshold: int = 32,
    gamma: float = 1.0,
    serpentine: bool = False,
) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    """
    Load an image, scale to dot_cap width, pick one horizontal line, and convert to dots list.

    Mapping:
      - x maps to idxNorm 0..65535
      - pixel RGB -> 3-bit rgbMask (TTL on/off) via threshold on each channel
        rgbMask bit0=R bit1=G bit2=B

    Params:
      line_index: row to sample after scaling; default center row
      threshold: 0..255 per-channel threshold for turning laser on
      gamma: apply gamma to pixel values before threshold (>=0.1)
      serpentine: if True, odd lines are reversed (useful if your scan alternates direction)

    Returns:
      (dots, meta) where meta includes scaled size and selected line.
    """
    rgb = _img_to_rgb_array(image_path)
    scaled = _scale_image_for_dots(rgb, dot_cap)

    H, W, _ = scaled.shape
    if line_index is None:
        line_index = H // 2
    line_index = int(max(0, min(H - 1, line_index)))

    line = scaled[line_index, :, :].astype(np.float32) / 255.0
    g = max(0.1, float(gamma))
    if g != 1.0:
        line = np.power(line, 1.0 / g)
    line_u8 = np.clip(line * 255.0, 0, 255).astype(np.uint8)

    if serpentine and (line_index % 2 == 1):
        line_u8 = line_u8[::-1, :]

    thr = int(max(0, min(255, threshold)))

    dots: List[Dict[str, Any]] = []
    # Build only "on" dots to save RMT items; skip black pixels
    for x in range(W):
        r, g_, b = (int(line_u8[x, 0]), int(line_u8[x, 1]), int(line_u8[x, 2]))
        mask = (1 if r >= thr else 0) | (2 if g_ >= thr else 0) | (4 if b >= thr else 0)
        if mask == 0:
            continue
    return {"status": "autoscan_stopped"}

# ------------------------- Device-level helpers -------------------------

def cmd_arm(value: bool) -> Dict[str, Any]:
    dl = client.send_json({"cmd": "arm", "value": bool(value)}, timeout_s=1.0)
    return dl.obj or {"raw": dl.raw}

def cmd_status() -> Dict[str, Any]:
    dl = client.send_json({"cmd": "status"}, timeout_s=1.0)
    return dl.obj or {"raw": dl.raw}

def cmd_telemetry() -> Dict[str, Any]:
    dl = client.send_json({"cmd": "get", "path": "*"}, timeout_s=1.5)
    return dl.obj or {"raw": dl.raw}

def cmd_set(path: str, value: Any) -> Dict[str, Any]:
    dl = client.send_json({"cmd": "set", "path": path, "value": value}, timeout_s=1.0)
    return dl.obj or {"raw": dl.raw}

def cmd_upload_dots(dots: List[Dict[str, Any]]) -> Dict[str, Any]:
    # firmware expects {"cmd":"dots.inactive","dots":[{idxNorm,rgbMask},...]}
    dl = client.send_json({"cmd": "dots.inactive", "dots": dots}, timeout_s=2.0)
    return dl.obj or {"raw": dl.raw}

def cmd_swap() -> Dict[str, Any]:
    dl = client.send_json({"cmd": "dots.swap", "value": True}, timeout_s=1.0)
    return dl.obj or {"raw": dl.raw}


# ------------------------- Dot generators -------------------------

def gen_single_dot(idx_norm: int, rgb_mask: int) -> List[Dict[str, Any]]:
    idx = max(0, min(65535, int(idx_norm)))
    mask = int(rgb_mask) & 0x07
    return [{"idxNorm": idx, "rgbMask": mask}]

def gen_line(n: int, rgb_mask: int) -> List[Dict[str, Any]]:
    n = int(n)
    n = max(1, min(DOT_CAP, n))
    mask = int(rgb_mask) & 0x07
    if n == 1:
        return [{"idxNorm": 32768, "rgbMask": mask}]
    out = []
    for i in range(n):
        idx = int(i * 65535 / (n - 1))
        out.append({"idxNorm": idx, "rgbMask": mask})
    return out

def gen_color_bars(n_per_color: int) -> List[Dict[str, Any]]:
    n = max(1, min(DOT_CAP // 3, int(n_per_color)))
    out: List[Dict[str, Any]] = []
    # R then G then B across the sweep
    for i in range(n):
        out.append({"idxNorm": int(i * 65535 / max(1, (n - 1))), "rgbMask": 0b001})
    for i in range(n):
        out.append({"idxNorm": int(i * 65535 / max(1, (n - 1))), "rgbMask": 0b010})
    for i in range(n):
        out.append({"idxNorm": int(i * 65535 / max(1, (n - 1))), "rgbMask": 0b100})
    # Sort by idxNorm so RMT builder is efficient/monotonic
    out.sort(key=lambda d: d["idxNorm"])
    return out[:DOT_CAP]

def parse_dots_json(text: str) -> Tuple[List[Dict[str, Any]], str]:
    text = (text or "").strip()
    if not text:
        return [], "empty"
    try:
        obj = json.loads(text)
    except Exception as e:
        return [], f"JSON parse error: {e}"

    dots: List[Dict[str, Any]] = []
    if isinstance(obj, dict) and "dots" in obj:
        obj = obj["dots"]

    if not isinstance(obj, list):
        return [], "expected a JSON list of dots or {dots:[...]}"
    for v in obj:
        if not isinstance(v, dict):
            continue
        idx = int(v.get("idxNorm", 0))
        mask = int(v.get("rgbMask", 0)) & 0x07
        idx = max(0, min(65535, idx))
        dots.append({"idxNorm": idx, "rgbMask": mask})
        if len(dots) >= DOT_CAP:
            break
    dots.sort(key=lambda d: d["idxNorm"])
    return dots, f"ok ({len(dots)} dots)"

# ------------------------- Gradio UI actions -------------------------

DOT_CAP = 1024

def ui_refresh_ports() -> List[Tuple[str, str]]:
    ports = client.list_ports()
    if not ports:
        return [("", "(no serial ports found)")]
    return ports

def ui_connect(port: str) -> Tuple[str, str]:
    if not port:
        port = client.autodetect_port()
    if not port:
        return "No port selected/found.", ""
    client.open(port)
    return f"Connected to {port}", port

def ui_disconnect() -> str:
    client.close()
    return "Disconnected."

def ui_arm(arm: bool) -> str:
    resp = cmd_arm(arm)
    return json.dumps(resp, indent=2)

def ui_status() -> str:
    resp = cmd_status()
    return json.dumps(resp, indent=2)

def ui_telemetry() -> str:
    resp = cmd_telemetry()
    return json.dumps(resp, indent=2)

def ui_set_ttl(pixel_width_us: int, extra_offset_us: int, ttl_freq_hz: int) -> str:
    r1 = cmd_set("ttl.pixelWidth_us", int(pixel_width_us))
    r2 = cmd_set("ttl.extraOffset_us", int(extra_offset_us))
    r3 = cmd_set("ttl.ttlFreq_hz", int(ttl_freq_hz))
    return json.dumps({"ttl.pixelWidth_us": r1, "ttl.extraOffset_us": r2, "ttl.ttlFreq_hz": r3}, indent=2)

def ui_set_dots_settings(test_enable: bool, test_count: int) -> str:
    r1 = cmd_set("dots.testPatternEnable", bool(test_enable))
    r2 = cmd_set("dots.testCount", int(test_count))
    return json.dumps({"dots.testPatternEnable": r1, "dots.testCount": r2}, indent=2)

def ui_make_pattern(kind: str, n: int, idx: int, rgb_mask: int) -> Tuple[str, str]:
    if kind == "single":
        dots = gen_single_dot(idx, rgb_mask)
    elif kind == "line":
        dots = gen_line(n, rgb_mask)
    else:
        dots = gen_color_bars(n)
    txt = json.dumps(dots, indent=2)
    return txt, f"generated {len(dots)} dots"

def ui_upload_and_swap(dots_json_text: str, do_swap: bool) -> str:
    dots, msg = parse_dots_json(dots_json_text)
    if not dots:
        return json.dumps({"error": "no_dots", "detail": msg}, indent=2)
    up = cmd_upload_dots(dots)
    out: Dict[str, Any] = {"upload": up}
    if do_swap:
        out["swap"] = cmd_swap()
    return json.dumps(out, indent=2)

def ui_swap_only() -> str:
    resp = cmd_swap()
    return json.dumps(resp, indent=2)

def ui_log_tail(n: int) -> str:
    lines = client.get_recent_lines(max_lines=max(1, int(n)))
    # show newest last
    out = []
    for dl in lines[-n:]:
        ts = time.strftime("%H:%M:%S", time.localtime(dl.t))
        out.append(f"{ts}  {dl.raw}")
    return "\n".join(out)


# ------------------------- Gradio app -------------------------

def build_app() -> gr.Blocks:
    with gr.Blocks(title="Resonant Mirror Controller") as demo:
        gr.Markdown("## Resonant Mirror Controller (ESP32) — Serial + RMT TTL\n"
                    "Connect, arm, upload dot buffers, and monitor telemetry.")
        with gr.Row():
            with gr.Column():
                gr.Markdown("### Serial Connection")
                port_dd = gr.Dropdown(
                    choices=ui_refresh_ports(),
                    label="Serial Port",
                    value=None,
                    interactive=True
                )
                with gr.Row():
                    conn_status = gr.Textbox(label="Connection Status", value="Disconnected.", interactive=False)
                    chosen_port = gr.Textbox(label="Connected Port", value="", interactive=False)
                with gr.Row():
                    refresh_btn = gr.Button("Refresh Ports")
                    connect_btn = gr.Button("Connect")
                    disconnect_btn = gr.Button("Disconnect")
                
                with gr.Row():
                    arm_toggle = gr.Checkbox(label="ARM", value=False)
                    arm_btn = gr.Button("Apply Arm")
                    status_btn = gr.Button("Status")
                    telem_btn = gr.Button("Telemetry (*)")
            with gr.Column():
                gr.Markdown("### TTL / Dot Settings")
                resp_box = gr.Textbox(label="Response")
                with gr.Row():
                    pixel_width = gr.Number(label="ttl.pixelWidth_us", value=1, precision=0)
                    extra_offset = gr.Number(label="ttl.extraOffset_us", value=0, precision=0)
                    ttl_freq = gr.Number(label="ttl.ttlFreq_hz (for minSpacing)", value=1000000, precision=0)
                    set_ttl_btn = gr.Button("Set TTL")
                set_ttl_btn.click(fn=ui_set_ttl, inputs=[pixel_width, extra_offset, ttl_freq], outputs=resp_box)
                with gr.Row():
                    test_enable = gr.Checkbox(label="dots.testPatternEnable", value=True)
                    test_count = gr.Number(label="dots.testCount", value=100, precision=0)
                    set_dots_btn = gr.Button("Set Dot Settings")
                set_dots_btn.click(fn=ui_set_dots_settings, inputs=[test_enable, test_count], outputs=resp_box)
                with gr.Row():
                    refresh_btn.click(fn=ui_refresh_ports, outputs=port_dd)
                    connect_btn.click(fn=ui_connect, inputs=port_dd, outputs=[conn_status, chosen_port])
                    disconnect_btn.click(fn=ui_disconnect, outputs=conn_status)
                    
                    arm_btn.click(fn=ui_arm, inputs=arm_toggle, outputs=resp_box)
                    status_btn.click(fn=ui_status, outputs=resp_box)
                    telem_btn.click(fn=ui_telemetry, outputs=resp_box)

       
        with gr.Row():
            
            with gr.Column():
                gr.Markdown("### Dot Pattern / Upload")
                with gr.Row():
                    pattern_kind = gr.Dropdown(choices=["single", "line", "color_bars"], value="line", label="Pattern")
                    n_dots = gr.Number(label="N (line/bars)", value=64, precision=0)
                    idx_norm = gr.Number(label="idxNorm (single)", value=32768, precision=0)
                rgb_mask = gr.Slider(0, 7, value=1, step=1, label="rgbMask (bit0=R bit1=G bit2=B)")
                gen_btn = gr.Button("Generate")        
            with gr.Column():
                gr.Markdown("### Image Line -> Dots")
                with gr.Row():
                    img_path = gr.Textbox(label="Image path on server", placeholder="e.g. ./test.png")
                    line_idx = gr.Number(label="Line index (scaled); blank=center", value=None, precision=0)
                    thr = gr.Slider(0, 255, value=32, step=1, label="Threshold")
                with gr.Row():
                    gam = gr.Number(label="Gamma", value=1.0, precision=2)
                    serp = gr.Checkbox(label="Serpentine reverse on odd lines", value=False)
                    img_swap = gr.Checkbox(label="Swap after upload", value=True)
                with gr.Row():
                    preview_btn = gr.Button("Generate (no send)")
                    send_line_btn = gr.Button("Send line now")
            with gr.Column():
                img_meta = gr.Code(label="Image meta", language="json")
                img_dots_preview = gr.Code(label="Dots preview (first 200)", language="json")

                def ui_preview_image_line(image_path, line_index, threshold, gamma, serpentine):
                    li = None
                    try:
                        if line_index is not None and str(line_index) != "nan":
                            li = int(line_index)
                    except Exception:
                        li = None
                    dots, meta = image_line_to_dots(image_path, li, DOT_CAP, int(threshold), float(gamma), bool(serpentine))
                    return json.dumps(meta, indent=2), json.dumps(dots[:200], indent=2)

                def ui_send_image_line(image_path, line_index, threshold, gamma, serpentine, do_swap):
                    li = None
                    try:
                        if line_index is not None and str(line_index) != "nan":
                            li = int(line_index)
                    except Exception:
                        li = None
                    resp = push_image_line(image_path, li, int(threshold), float(gamma), bool(serpentine), bool(do_swap))
                    return json.dumps(resp, indent=2)

                preview_btn.click(fn=ui_preview_image_line, inputs=[img_path, line_idx, thr, gam, serp],
                                outputs=[img_meta, img_dots_preview])
                send_line_btn.click(fn=ui_send_image_line, inputs=[img_path, line_idx, thr, gam, serp, img_swap],
                                    outputs=resp_box)
            with gr.Column():
                gr.Markdown("### Auto Image Scan")

                auto_start_line = gr.Number(label="Start line (scaled)", value=0, precision=0)
                auto_end_line = gr.Number(label="End line (scaled, inclusive; blank=last)", value=None, precision=0)
                auto_interval = gr.Number(label="Interval ms", value=50, precision=0)
                auto_loop = gr.Checkbox(label="Loop", value=True)

                with gr.Row():
                    auto_start_btn = gr.Button("Start Auto Scan")
                    auto_stop_btn = gr.Button("Stop Auto Scan")

                def ui_start_autoscan(image_path, start_line, end_line, interval_ms, threshold, gamma, serpentine, do_swap, loop):
                    e = None
                    try:
                        if end_line is not None and str(end_line) != "nan":
                            e = int(end_line)
                    except Exception:
                        e = None
                    return json.dumps(
                        start_auto_image_scan(
                            image_path=image_path,
                            start_line=int(start_line),
                            end_line=e,
                            interval_ms=int(interval_ms),
                            threshold=int(threshold),
                            gamma=float(gamma),
                            serpentine=bool(serpentine),
                            do_swap=bool(do_swap),
                            loop=bool(loop),
                        ),
                        indent=2
                    )

                def ui_stop_autoscan():
                    return json.dumps(stop_auto_image_scan(), indent=2)

                auto_start_btn.click(fn=ui_start_autoscan,
                                    inputs=[img_path, auto_start_line, auto_end_line, auto_interval, thr, gam, serp, img_swap, auto_loop],
                                    outputs=resp_box)
                auto_stop_btn.click(fn=ui_stop_autoscan, outputs=resp_box)
        dots_json = gr.Code(label="Dots JSON (list of {idxNorm,rgbMask})", language="json")
        gen_msg = gr.Textbox(label="Generator", value="", interactive=False)
        gen_btn.click(fn=ui_make_pattern, inputs=[pattern_kind, n_dots, idx_norm, rgb_mask], outputs=[dots_json, gen_msg])

        with gr.Row():
            do_swap = gr.Checkbox(label="Swap after upload", value=True)
            upload_btn = gr.Button("Upload Inactive (and optional swap)")
            swap_btn = gr.Button("Swap Only")

        upload_btn.click(fn=ui_upload_and_swap, inputs=[dots_json, do_swap], outputs=resp_box)
        swap_btn.click(fn=ui_swap_only, outputs=resp_box)

        gr.Markdown("### Serial Log")
        with gr.Row():
            log_n = gr.Number(label="Tail lines", value=50, precision=0)
            log_btn = gr.Button("Refresh Log")
        log_box = gr.Textbox(label="Log", lines=12, interactive=False)
        log_btn.click(fn=ui_log_tail, inputs=log_n, outputs=log_box)

    return demo


if __name__ == "__main__":
    app = build_app()
    # Set share=True if you want a public link
    app.launch(server_name="0.0.0.0", server_port=7860)