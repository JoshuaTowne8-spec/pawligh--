"""Seven editable light presets shared by the browser, gateway and ESP32."""

from copy import deepcopy
import json
from pathlib import Path
import re

PRESET_ORDER = ["warm", "happy", "calm", "miss", "sad", "quick_touch", "slow_touch"]
EMOTION_LABELS = {
    "warm": "温暖",
    "happy": "快乐",
    "calm": "平静",
    "miss": "想念",
    "sad": "难过",
    "quick_touch": "快速触摸",
    "slow_touch": "慢速触摸",
}
EFFECT_TYPES = {"solid", "breath", "wave", "center_breath", "chase", "sparkle", "heartbeat"}
HEX_COLOR = re.compile(r"^#[0-9a-fA-F]{6}$")

DEFAULT_EFFECTS = {
    "warm": {"label": "温暖", "type": "breath", "color1": "#F2A07B", "color2": "#FFD7B5", "brightness": 72, "speed": 30, "period_ms": 2600, "direction": "inward", "mirror": True, "sparkle": 0},
    "happy": {"label": "快乐", "type": "sparkle", "color1": "#FFB347", "color2": "#FFE08A", "brightness": 96, "speed": 75, "period_ms": 900, "direction": "forward", "mirror": True, "sparkle": 18},
    "calm": {"label": "平静", "type": "breath", "color1": "#FFE4B5", "color2": "#D9F2E6", "brightness": 52, "speed": 18, "period_ms": 4200, "direction": "inward", "mirror": True, "sparkle": 0},
    "miss": {"label": "想念", "type": "center_breath", "color1": "#E99579", "color2": "#FFD0B5", "brightness": 78, "speed": 28, "period_ms": 3000, "direction": "center_out", "mirror": True, "sparkle": 2},
    "sad": {"label": "难过", "type": "wave", "color1": "#254A87", "color2": "#7A9FD1", "brightness": 42, "speed": 16, "period_ms": 3600, "direction": "backward", "mirror": True, "sparkle": 0},
    "quick_touch": {"label": "快速触摸", "type": "chase", "color1": "#FFD166", "color2": "#FF8C42", "brightness": 120, "speed": 100, "period_ms": 1500, "direction": "forward", "mirror": True, "sparkle": 4},
    "slow_touch": {"label": "慢速触摸", "type": "breath", "color1": "#E9A28F", "color2": "#FFE0C2", "brightness": 86, "speed": 25, "period_ms": 3000, "direction": "inward", "mirror": True, "sparkle": 0},
}


def _default_copy() -> dict:
    return deepcopy(DEFAULT_EFFECTS)


def load_effects(path: Path) -> dict:
    effects = _default_copy()
    if path.exists():
        try:
            saved = json.loads(path.read_text(encoding="utf-8"))
            for preset in PRESET_ORDER:
                if isinstance(saved.get(preset), dict):
                    effects[preset].update(saved[preset])
        except (OSError, ValueError, TypeError):
            pass
    return effects


def save_effects(path: Path, effects: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(effects, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


def validate_effect(value: dict, preset: str) -> dict:
    if preset not in PRESET_ORDER:
        raise ValueError("unknown preset")
    if not isinstance(value, dict):
        raise ValueError("effect must be an object")
    result = deepcopy(DEFAULT_EFFECTS[preset])
    result.update(value)
    result["label"] = EMOTION_LABELS[preset]
    if result["type"] not in EFFECT_TYPES:
        raise ValueError("unsupported effect type")
    for key in ("color1", "color2"):
        if not isinstance(result[key], str) or not HEX_COLOR.fullmatch(result[key]):
            raise ValueError(f"invalid {key}")
    for key, low, high in (("brightness", 1, 128), ("speed", 1, 100), ("period_ms", 200, 20000), ("sparkle", 0, 100)):
        try:
            result[key] = max(low, min(high, int(result[key])))
        except (ValueError, TypeError):
            raise ValueError(f"invalid {key}") from None
    result["mirror"] = bool(result.get("mirror", True))
    result["direction"] = str(result.get("direction", "forward"))[:16]
    return result
