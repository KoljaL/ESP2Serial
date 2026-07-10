Import("env")

import json
import os
from pathlib import Path


PROJECT_DIR = Path(env["PROJECT_DIR"])
ENV_PATH = PROJECT_DIR / ".env"

REQUIRED_KEYS = (
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "OTA_HOSTNAME",
)

OPTIONAL_KEYS = (
    "FALLBACK_AP_SSID",
    "FALLBACK_AP_PASSWORD",
    "OTA_TARGET_IP",
)


def parse_env_file(path: Path):
    values = {}

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        if "=" not in line:
            raise ValueError(f"Invalid .env line: {raw_line}")

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()

        if len(value) >= 2 and value[0] == value[-1] and value[0] in ('\"', "'"):
            value = value[1:-1]

        values[key] = value

    return values


if not ENV_PATH.exists():
    raise FileNotFoundError(
        f"Missing {ENV_PATH}. Copy .env.example to .env and fill in your values."
    )

config = parse_env_file(ENV_PATH)

missing = [key for key in REQUIRED_KEYS if not config.get(key)]
if missing:
    raise ValueError(
        "Missing required .env values: " + ", ".join(missing)
    )

for key in REQUIRED_KEYS + OPTIONAL_KEYS:
    if key in config:
        escaped = json.dumps(config[key])[1:-1]
        env.Append(CPPDEFINES=[(key, f'\\"{escaped}\\"')])

ota_target = os.environ.get("ESP8266_OTA_TARGET")

if env["PIOENV"].endswith("_ota"):
    if not ota_target:
        ota_target = config.get("OTA_TARGET_IP", "").strip()

    is_upload_target = "upload" in COMMAND_LINE_TARGETS

    if not ota_target and is_upload_target:
        raise ValueError(
            "Missing OTA target IP. Set ESP8266_OTA_TARGET in your shell or "
            "OTA_TARGET_IP in esp8266-target/.env before OTA upload."
        )

    if ota_target:
        env.Replace(UPLOAD_PORT=ota_target)
