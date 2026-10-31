"""Gera as falas do paciente (.wav) a partir de falas_paciente.csv usando Edge-TTS (vozes neurais de alta fidelidade).

Saída: WAV mono, 16-bit, 44.1 kHz em ./output, com visemas gerados pelo Rhubarb e DT_FalasPaciente.csv.
"""
import argparse
import asyncio
import csv
import io
import json
import subprocess
import wave
from pathlib import Path

import edge_tts
import miniaudio

ROOT = Path(__file__).resolve().parent
TARGET_RATE = 44100
RHUBARB = next((ROOT.parent / "LipSync" / "rhubarb").glob("**/rhubarb.exe"), None)

def write_wav(path: Path, raw_bytes: bytes) -> None:
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(TARGET_RATE)
        wav_file.writeframes(raw_bytes)

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

async def synth_line(text: str, voice: str, rate: str = "+0%") -> bytes:
    communicate = edge_tts.Communicate(text, voice, rate=rate)
    buf = io.BytesIO()
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            buf.write(chunk["data"])
    buf.seek(0)
    decoded = miniaudio.decode(buf.read(), nchannels=1, sample_rate=TARGET_RATE, output_format=miniaudio.SampleFormat.SIGNED16)
    return decoded.samples

async def main_async():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=ROOT / "falas_paciente.csv")
    parser.add_argument("--out", type=Path, default=ROOT / "output")
    parser.add_argument("--voice", default="pt-BR-AntonioNeural", help="Voz Edge (ex.: pt-BR-AntonioNeural, pt-BR-FabioNeural, pt-BR-NicolauNeural)")
    parser.add_argument("--rate", default="+0%", help="Ajuste de velocidade (ex.: -5%%, +0%%, +5%%)")
    parser.add_argument("--ue-path", default="/Game/Audio/Paciente")
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()

    args.out.mkdir(exist_ok=True)
    with args.csv.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))

    print(f"Gerando falas com Edge-TTS ({args.voice})...")
    for row in rows:
        if args.only and row["id"] not in args.only:
            continue
        text = (row.get("texto_tts") or "").strip() or row["texto"].strip()
        raw_pcm = await synth_line(text, args.voice, args.rate)
        wav_path = args.out / f"{row['id']}.wav"
        write_wav(wav_path, raw_pcm)

        if RHUBARB:
            visemas = lip_sync(wav_path)
            (args.out / f"{row['id']}.visemas.txt").write_text(visemas, encoding="utf-8")
        
        duration = len(raw_pcm) / (TARGET_RATE * 2)
        print(f"  {row['id']}.wav  ({duration:.1f}s)")

    # Escreve DT_FalasPaciente.csv
    with (args.out / "DT_FalasPaciente.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["---", "Passo", "Gatilho", "Texto", "Som", "Visemas", "Disparo"])
        for row in rows:
            asset = f"{args.ue_path}/{row['id']}.{row['id']}"
            visemes_file = args.out / f"{row['id']}.visemas.txt"
            visemes = visemes_file.read_text(encoding="utf-8") if visemes_file.exists() else ""
            writer.writerow([row["id"], row["passo"], row["gatilho"], row["texto"], asset, visemes, row.get("disparo", "")])

    print(f"\nPronto: {args.out}")

if __name__ == "__main__":
    asyncio.run(main_async())

