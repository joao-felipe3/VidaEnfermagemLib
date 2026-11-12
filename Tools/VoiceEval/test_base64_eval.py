import base64
import json
import urllib.request
import time

API_URL = "https://voice-eval-api.onrender.com"
API_KEY = "wcP90Sipv_00QbUM8O_NBDsALc76AJSX6bt1A2aXLtg"

# Use one of the voice files or synthesize speech
# Let's test with P1_acolhimento.wav first
wav_path = "Tools/VoiceGen/output/P1_acolhimento.wav"
with open(wav_path, "rb") as f:
    wav_bytes = f.read()

b64_audio = base64.b64encode(wav_bytes).decode("utf-8")
print(f"WAV loaded: {len(wav_bytes)} bytes -> {len(b64_audio)} chars Base64")

payload = {
    "scenario_id": "coleta-sangue",
    "question_id": "apresentacao",
    "audio_base64": b64_audio,
    "audio_format": "wav"
}

req = urllib.request.Request(
    f"{API_URL}/v1/evaluate/base64",
    data=json.dumps(payload).encode("utf-8"),
    headers={
        "x-api-key": API_KEY,
        "Content-Type": "application/json",
        "User-Agent": "UnrealEngine-MedicalGestureLib"
    }
)

start = time.time()
try:
    with urllib.request.urlopen(req, timeout=40) as resp:
        res = json.loads(resp.read().decode())
        print(f"API Response (in {time.time() - start:.2f}s):")
        print(json.dumps(res, indent=2, ensure_ascii=False))
except urllib.error.HTTPError as e:
    print(f"HTTP Error {e.code}: {e.read().decode()}")
except Exception as e:
    print(f"Error: {e}")

