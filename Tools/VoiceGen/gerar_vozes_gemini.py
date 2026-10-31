"""Gera as falas do paciente usando Google AI Studio (Gemini 3.8 Flash TTS).

Saída: WAV mono, 16-bit, 44.1 kHz em ./output, com visemas gerados pelo Rhubarb e DT_FalasPaciente.csv.
"""
import argparse
import csv
import io
import json
import os
import subprocess
import time
import wave
from pathlib import Path

from dotenv import load_dotenv
from google import genai
from google.genai import types
import numpy as np

ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent.parent
load_dotenv(PROJECT_ROOT / ".env")

TARGET_RATE = 44100
RHUBARB = next((ROOT.parent / "LipSync" / "rhubarb").glob("**/rhubarb.exe"), None)

def resample_wav_bytes(wav_bytes: bytes, target_rate: int = TARGET_RATE) -> bytes:
    with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
        src_rate = wf.getframerate()
        n_channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        n_frames = wf.getnframes()
        raw_frames = wf.readframes(n_frames)

    samples = np.frombuffer(raw_frames, dtype=np.int16).astype(np.float32) / 32768.0
    if n_channels > 1:
        samples = samples.reshape(-1, n_channels).mean(axis=1)

    if src_rate != target_rate:
        duration = len(samples) / src_rate
        src_t = np.arange(len(samples)) / src_rate
        dst_t = np.arange(int(round(duration * target_rate))) / target_rate
        samples = np.interp(dst_t, src_t, samples).astype(np.float32)

    peak = float(np.max(np.abs(samples))) or 1.0
    samples = samples * (10 ** (-2.0 / 20) / peak)
    int_samples = (np.clip(samples, -1.0, 1.0) * 32767).astype(np.int16)

    out_buf = io.BytesIO()
    with wave.open(out_buf, "wb") as out_wf:
        out_wf.setnchannels(1)
        out_wf.setsampwidth(2)
        out_wf.setframerate(target_rate)
        out_wf.writeframes(int_samples.tobytes())
    return out_buf.getvalue()

def lip_sync(wav_path: Path) -> str:
    if not RHUBARB:
        return ""
    json_path = wav_path.with_suffix(".rhubarb.json")
    subprocess.run(
        [str(RHUBARB), "-r", "phonetic", "-f", "json", "--extendedShapes", "GHX", "--quiet",
         "-o", str(json_path), str(wav_path)],
        check=True, capture_output=True,
    )
    cues = json.loads(json_path.read_text(encoding="utf-8"))["mouthCues"]
    json_path.unlink(missing_ok=True)
    return ";".join(f"{cue['start']:.2f}:{cue['value']}" for cue in cues)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=ROOT / "falas_paciente.csv")
    parser.add_argument("--out", type=Path, default=ROOT / "output")
    parser.add_argument("--voice", default="Puck", help="Voz do Gemini (Puck, Charon, Fenrir, Aoede, Kore)")
    parser.add_argument("--ue-path", default="/Game/Audio/Paciente")
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()

    api_key = os.getenv("AI_STUDIO_KEY") or os.getenv("GEMINI_API_KEY")
    if not api_key:
        raise ValueError("Chave de API AI_STUDIO_KEY não encontrada no arquivo .env!")

    client = genai.Client(api_key=api_key)
    args.out.mkdir(exist_ok=True)

    with args.csv.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))

    print(f"============================================================")
    print(f"Gerando falas com Google AI Studio - Gemini 3.8 Flash TTS")
    print(f"Voz: {args.voice} (Jovem 24 anos) | Alvo: 44.1 kHz mono WAV")
    print(f"============================================================")

    for i, row in enumerate(rows, 1):
        if args.only and row["id"] not in args.only:
            continue
        text = (row.get("texto_tts") or "").strip() or row["texto"].strip()
        print(f"[{i}/{len(rows)}] Gerando {row['id']}...")

        raw_wav_bytes = None
        for attempt in range(5):
            try:
                resp = client.models.generate_content(
                    model="gemini-3.8-flash-tts",
                    contents=text,
                    config=types.GenerateContentConfig(
                        response_modalities=["AUDIO"],
                        speech_config=types.SpeechConfig(
                            voice_config=types.VoiceConfig(
                                prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name=args.voice)
                            )
                        )
                    )
                )
                for part in resp.candidates[0].content.parts:
                    if part.inline_data:
                        raw_wav_bytes = part.inline_data.data
                        break
                if raw_wav_bytes:
                    break
            except Exception as e:
                wait_sec = 6 * (attempt + 1)
                print(f"  Tentativa {attempt + 1} falhou ({e}). Aguardando {wait_sec}s...")
                time.sleep(wait_sec)

        if not raw_wav_bytes:
            print(f"  [ERRO] Não foi possível gerar {row['id']}")
            continue

        resampled_wav = resample_wav_bytes(raw_wav_bytes, TARGET_RATE)
        wav_path = args.out / f"{row['id']}.wav"
        wav_path.write_bytes(resampled_wav)

        if RHUBARB:
            visemas = lip_sync(wav_path)
            (args.out / f"{row['id']}.visemas.txt").write_text(visemas, encoding="utf-8")

        duration = (len(resampled_wav) - 44) / (TARGET_RATE * 2)
        print(f"  -> {row['id']}.wav ({duration:.1f}s) + visemas prontos")
        time.sleep(2)

    # Atualiza a tabela DT_FalasPaciente.csv
    with (args.out / "DT_FalasPaciente.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["---", "Passo", "Gatilho", "Texto", "Som", "Visemas", "Disparo"])
        for row in rows:
            asset = f"{args.ue_path}/{row['id']}.{row['id']}"
            visemes_file = args.out / f"{row['id']}.visemas.txt"
            visemes = visemes_file.read_text(encoding="utf-8") if visemes_file.exists() else ""
            writer.writerow([row["id"], row["passo"], row["gatilho"], row["texto"], asset, visemes, row.get("disparo", "")])

    print(f"\n[SUCESSO] Todas as 18 falas foram geradas e salvas em: {args.out}")

if __name__ == "__main__":
    main()
