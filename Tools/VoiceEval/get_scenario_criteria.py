import urllib.request
import json

url = "https://voice-eval-api.onrender.com/v1/scenarios/coleta-sangue"
req = urllib.request.Request(url, headers={
    "x-api-key": "wcP90Sipv_00QbUM8O_NBDsALc76AJSX6bt1A2aXLtg",
    "User-Agent": "VoiceEvalTester/1.0"
})

with urllib.request.urlopen(req, timeout=30) as resp:
    data = json.loads(resp.read().decode())
    for q in data.get("questions", []):
        if "experiencia" in q["id"] or "consentimento" in q["id"]:
            print(f"=== ID: {q['id']} ===")
            print(f"Hint: {q.get('hint')}")
            for c in q.get("criteria", []):
                print(f"  Criterion [{c['id']}] (Req: {c['required']}):")
                print(f"    Description: {c.get('description')}")
            for err in q.get("critical_errors", []):
                print(f"  Critical Error [{err['id']}]: {err.get('description')}")
            print()

