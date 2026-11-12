import urllib.request
import json

url = "https://voice-eval-api.onrender.com/v1/scenarios/coleta-sangue"
req = urllib.request.Request(url, headers={
    "x-api-key": "wcP90Sipv_00QbUM8O_NBDsALc76AJSX6bt1A2aXLtg",
    "User-Agent": "VoiceEvalTester/1.0"
})

with urllib.request.urlopen(req, timeout=30) as resp:
    data = json.loads(resp.read().decode())
    print("Scenario:", data.get("id"), "| Title:", data.get("title"))
    print("=" * 60)
    for q in data.get("questions", []):
        print(f"ID: {q['id']}")
        print(f"  Prompt: {q.get('prompt')}")
        print(f"  Hint:   {q.get('hint')}")
        for c in q.get("criteria", []):
            req_str = "OBRIGATÓRIO" if c.get("required") else "opcional"
            print(f"    * [{c['id']}] ({req_str}): {c.get('description')}")
        print("-" * 60)

