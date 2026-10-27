"""Gera as falas do paciente (.wav) a partir de falas_paciente.csv usando Piper (offline).

Saída: WAV mono, 16-bit, 44.1 kHz em ./output, mais DT_FalasPaciente.csv pronto para
importar como DataTable (struct FPatientVoiceLine) na Unreal.

Uso:
    .venv\\Scripts\\python gerar_vozes.py                 # gera todas as falas
    .venv\\Scripts\\python gerar_vozes.py --only P2_nome_data P5_consentimento
    .venv\\Scripts\\python gerar_vozes.py --voice pt_BR-cadu-medium --speed 0.95
"""

import argparse
import csv
import io
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np
from piper import PiperVoice, SynthesisConfig

ROOT = Path(__file__).resolve().parent
MODELS_DIR = ROOT / "models"
TARGET_RATE = 44100
PEAK_DBFS = -3.0
PAD_START_S = 0.05
PAD_END_S = 0.20


def ensure_voice(voice_name: str) -> Path:
    model = MODELS_DIR / f"{voice_name}.onnx"
    if not model.exists():
        MODELS_DIR.mkdir(exist_ok=True)
        print(f"Baixando voz {voice_name}...")
        subprocess.run(
            [sys.executable, "-m", "piper.download_voices", voice_name, "--download-dir", str(MODELS_DIR)],
            check=True,
        )
    return model


def synthesize(voice: PiperVoice, text: str, config: SynthesisConfig) -> tuple[np.ndarray, int]:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav_file:
        voice.synthesize_wav(text, wav_file, syn_config=config)
    buffer.seek(0)
    with wave.open(buffer, "rb") as wav_file:
        rate = wav_file.getframerate()
        samples = np.frombuffer(wav_file.readframes(wav_file.getnframes()), dtype=np.int16)
    return samples.astype(np.float32) / 32768.0, rate


def resample(samples: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
    if src_rate == dst_rate:
        return samples
    duration = len(samples) / src_rate
    src_t = np.arange(len(samples)) / src_rate
    dst_t = np.arange(int(round(duration * dst_rate))) / dst_rate
    return np.interp(dst_t, src_t, samples).astype(np.float32)


def finalize(samples: np.ndarray, rate: int) -> np.ndarray:
    peak = float(np.max(np.abs(samples))) or 1.0
    samples = samples * (10 ** (PEAK_DBFS / 20) / peak)
    samples = np.concatenate([
        np.zeros(int(PAD_START_S * rate), dtype=np.float32),
        samples,
        np.zeros(int(PAD_END_S * rate), dtype=np.float32),
    ])
    return (np.clip(samples, -1.0, 1.0) * 32767).astype(np.int16)


def write_wav(path: Path, samples: np.ndarray, rate: int) -> None:
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(rate)
        wav_file.writeframes(samples.tobytes())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", type=Path, default=ROOT / "falas_paciente.csv")
    parser.add_argument("--out", type=Path, default=ROOT / "output")
    parser.add_argument("--voice", default="pt_BR-faber-medium", help="Voz Piper (ex.: pt_BR-faber-medium, pt_BR-cadu-medium, pt_BR-jeff-medium)")
    parser.add_argument("--speed", type=float, default=1.0, help="<1 mais lento, >1 mais rápido")
    parser.add_argument("--only", nargs="*", help="Gera apenas estes ids")
    parser.add_argument("--ue-path", default="/Game/Audio/Paciente", help="Pasta onde os .wav serão importados na Unreal")
    args = parser.parse_args()

    with args.csv.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))

    voice = PiperVoice.load(ensure_voice(args.voice))
    config = SynthesisConfig(length_scale=1.0 / args.speed)
    args.out.mkdir(exist_ok=True)

    for row in rows:
        if args.only and row["id"] not in args.only:
            continue
        text = (row.get("texto_tts") or "").strip() or row["texto"].strip()
        samples, rate = synthesize(voice, text, config)
        samples = finalize(resample(samples, rate, TARGET_RATE), TARGET_RATE)
        write_wav(args.out / f"{row['id']}.wav", samples, TARGET_RATE)
        print(f"  {row['id']}.wav  ({len(samples) / TARGET_RATE:.1f}s)")

    # DataTable da Unreal: a primeira coluna vira o nome da linha. BOM garante acentos no import.
    with (args.out / "DT_FalasPaciente.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["---", "Passo", "Gatilho", "Texto", "Som"])
        for row in rows:
            asset = f"{args.ue_path}/{row['id']}.{row['id']}"
            writer.writerow([row["id"], row["passo"], row["gatilho"], row["texto"], asset])

    print(f"Pronto: {args.out}")


if __name__ == "__main__":
    main()
