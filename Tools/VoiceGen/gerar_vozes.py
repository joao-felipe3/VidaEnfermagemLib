"""Gera as falas do paciente (.wav) a partir de falas_paciente.csv usando Piper (offline).

Saída: WAV mono, 16-bit, 44.1 kHz em ./output, mais DT_FalasPaciente.csv pronto para
importar como DataTable (struct FPatientVoiceLine) na Unreal. Se o Rhubarb Lip Sync estiver em
../LipSync/rhubarb, cada fala também ganha a sequência de visemas (coluna Visemas).

Uso:
    .venv\\Scripts\\python gerar_vozes.py                 # gera todas as falas
    .venv\\Scripts\\python gerar_vozes.py --only P2_nome_data P5_consentimento
    .venv\\Scripts\\python gerar_vozes.py --voice pt_BR-cadu-medium --speed 0.95

Frases longas são sintetizadas em pedaços (entre vírgulas e pontos) e unidas com pausas curtas:
o Piper perde a entonação em frases compridas. Um equalizador leve tira o som fanho da voz.
"""

import argparse
import csv
import io
import json
import re
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np
from piper import PiperVoice, SynthesisConfig
from scipy.signal import lfilter

ROOT = Path(__file__).resolve().parent
MODELS_DIR = ROOT / "models"
TARGET_RATE = 44100
PEAK_DBFS = -3.0
PAD_START_S = 0.05
PAD_END_S = 0.20
RHUBARB = next((ROOT.parent / "LipSync" / "rhubarb").glob("**/rhubarb.exe"), None)

# Pausa (s) inserida depois de cada pedaço, conforme a pontuação que o encerra.
PAUSES = {",": 0.14, ";": 0.2, ".": 0.3, "?": 0.3, "!": 0.3}
# Equalizador (frequência Hz, ganho dB, Q): menos ressonância nasal, mais corpo e clareza.
EQ_BANDS = [(220, 2.0, 0.8), (1000, -4.0, 1.2), (3200, 2.5, 1.0)]
HIGHPASS_HZ = 70


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


def split_phrases(text: str) -> list[tuple[str, float]]:
    """Pedaços terminados em pontuação, com a pausa que vem depois. Pedaços de 1-2 palavras
    seguidos de vírgula ("Opa, opa,") ficam juntos com o próximo para não picotar a fala."""
    phrases: list[tuple[str, float]] = []
    carry = ""
    for part in re.findall(r"[^,.;?!]+[,.;?!]*", text):
        part = (carry + " " + part).strip() if carry else part.strip()
        carry = ""
        if not part:
            continue
        if part.endswith(",") and len(part.split()) < 3:
            carry = part
            continue
        phrases.append((part, PAUSES.get(part[-1], 0.0)))
    if carry:
        phrases.append((carry, 0.0))
    return phrases


def trim_silence(samples: np.ndarray, rate: int, floor_db: float = -38.0) -> np.ndarray:
    """Tira o silêncio/ruído das bordas (energia por janela de 20 ms abaixo de floor_db do pico),
    com folga generosa no fim para não comer finais suaves como "-lha"."""
    frame = int(0.02 * rate)
    frames = len(samples) // frame
    if frames == 0:
        return samples
    rms = np.sqrt(np.mean(samples[:frames * frame].reshape(frames, frame) ** 2, axis=1))
    loud = np.flatnonzero(rms > rms.max() * 10 ** (floor_db / 20))
    start = max(loud[0] * frame - int(0.02 * rate), 0)
    end = (loud[-1] + 1) * frame + int(0.1 * rate)
    return samples[start:end]


def synthesize_phrases(voice: PiperVoice, text: str, config: SynthesisConfig) -> tuple[np.ndarray, int]:
    pieces, rate = [], 22050
    for phrase, pause in split_phrases(text):
        samples, rate = synthesize(voice, phrase, config)
        pieces += [trim_silence(samples, rate), np.zeros(int(pause * rate), dtype=np.float32)]
    return np.concatenate(pieces), rate


def biquad_peak(freq: float, gain_db: float, q: float, rate: int) -> tuple[np.ndarray, np.ndarray]:
    """Filtro peaking do RBJ Audio EQ Cookbook."""
    a = 10 ** (gain_db / 40)
    w = 2 * np.pi * freq / rate
    alpha = np.sin(w) / (2 * q)
    b = [1 + alpha * a, -2 * np.cos(w), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w), 1 - alpha / a]
    return np.array(b) / den[0], np.array(den) / den[0]


def biquad_highpass(freq: float, rate: int, q: float = 0.707) -> tuple[np.ndarray, np.ndarray]:
    w = 2 * np.pi * freq / rate
    alpha = np.sin(w) / (2 * q)
    b = [(1 + np.cos(w)) / 2, -(1 + np.cos(w)), (1 + np.cos(w)) / 2]
    den = [1 + alpha, -2 * np.cos(w), 1 - alpha]
    return np.array(b) / den[0], np.array(den) / den[0]


def equalize(samples: np.ndarray, rate: int) -> np.ndarray:
    filters = [biquad_highpass(HIGHPASS_HZ, rate)] + [biquad_peak(f, g, q, rate) for f, g, q in EQ_BANDS]
    for b, a in filters:
        samples = lfilter(b, a, samples)
    return samples.astype(np.float32)


def render(voice: PiperVoice, text: str, config: SynthesisConfig, eq: bool = True) -> np.ndarray:
    """Texto -> amostras int16 a 44.1 kHz, prontas para gravar."""
    samples, rate = synthesize_phrases(voice, text, config)
    samples = resample(samples, rate, TARGET_RATE)
    if eq:
        samples = equalize(samples, TARGET_RATE)
    return finalize(samples, TARGET_RATE)


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
    parser.add_argument("--voice", default="pt_BR-jeff-medium", help="Voz Piper (ex.: pt_BR-jeff-medium, pt_BR-faber-medium, pt_BR-cadu-medium)")
    parser.add_argument("--speed", type=float, default=0.95, help="<1 mais lento, >1 mais rápido")
    parser.add_argument("--noise", type=float, default=0.45, help="Variação da entonação (padrão do Piper: 0.667)")
    parser.add_argument("--noise-w", type=float, default=0.55, help="Variação do ritmo (padrão do Piper: 0.8)")
    parser.add_argument("--sem-eq", action="store_true", help="Não aplica o equalizador")
    parser.add_argument("--only", nargs="*", help="Gera apenas estes ids")
    parser.add_argument("--ue-path", default="/Game/Audio/Paciente", help="Pasta onde os .wav serão importados na Unreal")
    parser.add_argument("--sem-lipsync", action="store_true", help="Não gera visemas com o Rhubarb")
    parser.add_argument("--so-tabela", action="store_true", help="Só reescreve DT_FalasPaciente.csv, sem gerar áudio")
    args = parser.parse_args()
    use_lip_sync = RHUBARB is not None and not args.sem_lipsync
    if RHUBARB is None and not args.sem_lipsync:
        print("Aviso: Rhubarb não encontrado em ../LipSync/rhubarb; gerando sem visemas.")

    with args.csv.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))

    args.out.mkdir(exist_ok=True)
    if not args.so_tabela:
        voice = PiperVoice.load(ensure_voice(args.voice))
        config = SynthesisConfig(length_scale=1.0 / args.speed, noise_scale=args.noise, noise_w_scale=args.noise_w)

    for row in rows:
        if args.so_tabela or (args.only and row["id"] not in args.only):
            continue
        text = (row.get("texto_tts") or "").strip() or row["texto"].strip()
        samples = render(voice, text, config, eq=not args.sem_eq)
        wav_path = args.out / f"{row['id']}.wav"
        write_wav(wav_path, samples, TARGET_RATE)
        if use_lip_sync:
            (args.out / f"{row['id']}.visemas.txt").write_text(lip_sync(wav_path), encoding="utf-8")
        print(f"  {row['id']}.wav  ({len(samples) / TARGET_RATE:.1f}s)")

    # DataTable da Unreal: a primeira coluna vira o nome da linha. BOM garante acentos no import.
    with (args.out / "DT_FalasPaciente.csv").open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["---", "Passo", "Gatilho", "Texto", "Som", "Visemas", "Disparo"])
        for row in rows:
            asset = f"{args.ue_path}/{row['id']}.{row['id']}"
            visemes_file = args.out / f"{row['id']}.visemas.txt"
            visemes = visemes_file.read_text(encoding="utf-8") if visemes_file.exists() else ""
            writer.writerow([row["id"], row["passo"], row["gatilho"], row["texto"], asset, visemes,
                             row.get("disparo", "")])

    print(f"Pronto: {args.out}")


if __name__ == "__main__":
    main()
