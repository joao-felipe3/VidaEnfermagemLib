"""Gera as falas do paciente (.wav) a partir de falas_paciente.csv usando Piper (offline).

Saída: WAV mono, 16-bit, 44.1 kHz em ./output, mais DT_FalasPaciente.csv pronto para
importar como DataTable (struct FPatientVoiceLine) na Unreal. Se o Rhubarb Lip Sync estiver em
../LipSync/rhubarb, cada fala também ganha a sequência de visemas (coluna Visemas).

Uso:
    .venv\\Scripts\\python gerar_vozes.py                 # gera todas as falas
    .venv\\Scripts\\python gerar_vozes.py --only P2_nome_data P5_consentimento
    .venv\\Scripts\\python gerar_vozes.py --voice pt_BR-cadu-medium --speed 0.95
"""

import argparse
import csv
import io
import json
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
RHUBARB = next((ROOT.parent / "LipSync" / "rhubarb").glob("**/rhubarb.exe"), None)


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


def lip_sync(wav_path: Path) -> str:
    """Formas de boca do Rhubarb (A-H, X) como "inicio:forma;..." para o DataTable."""
    json_path = wav_path.with_suffix(".rhubarb.json")
    subprocess.run(
        [str(RHUBARB), "-r", "phonetic", "-f", "json", "--extendedShapes", "GHX", "--quiet",
         "-o", str(json_path), str(wav_path)],
        check=True, capture_output=True,
    )
    cues = json.loads(json_path.read_text(encoding="utf-8"))["mouthCues"]
    json_path.unlink()
    return ";".join(f"{cue['start']:.2f}:{cue['value']}" for cue in cues)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", type=Path, default=ROOT / "falas_paciente.csv")
    parser.add_argument("--out", type=Path, default=ROOT / "output")
    parser.add_argument("--voice", default="pt_BR-faber-medium", help="Voz Piper (ex.: pt_BR-faber-medium, pt_BR-cadu-medium, pt_BR-jeff-medium)")
    parser.add_argument("--speed", type=float, default=1.0, help="<1 mais lento, >1 mais rápido")
    parser.add_argument("--only", nargs="*", help="Gera apenas estes ids")
    parser.add_argument("--ue-path", default="/Game/Audio/Paciente", help="Pasta onde os .wav serão importados na Unreal")
    parser.add_argument("--sem-lipsync", action="store_true", help="Não gera visemas com o Rhubarb")
    args = parser.parse_args()
    use_lip_sync = RHUBARB is not None and not args.sem_lipsync
    if RHUBARB is None and not args.sem_lipsync:
        print("Aviso: Rhubarb não encontrado em ../LipSync/rhubarb; gerando sem visemas.")

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
        wav_path = args.out / f"{row['id']}.wav"
        write_wav(wav_path, samples, TARGET_RATE)
        if use_lip_sync:
            (args.out / f"{row['id']}.visemas.txt").write_text(lip_sync(wav_path), encoding="utf-8")
        print(f"  {row['id']}.wav  ({len(samples) / TARGET_RATE:.1f}s)")

    # DataTable da Unreal: a primeira coluna vira o nome da linha. BOM garante acentos no import.
    with (args.out / "DT_FalasPaciente.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["---", "Passo", "Gatilho", "Texto", "Som", "Visemas"])
        for row in rows:
            asset = f"{args.ue_path}/{row['id']}.{row['id']}"
            visemes_file = args.out / f"{row['id']}.visemas.txt"
            visemes = visemes_file.read_text(encoding="utf-8") if visemes_file.exists() else ""
            writer.writerow([row["id"], row["passo"], row["gatilho"], row["texto"], asset, visemes])

    print(f"Pronto: {args.out}")


if __name__ == "__main__":
    main()
