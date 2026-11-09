"""Visemas a partir do texto da fala, alinhados ao áudio pelos tempos das palavras do Whisper.

O Rhubarb reconhece fonemas em inglês e, em português, gera poucas formas de boca (~6 por segundo)
que ficam paradas por meio segundo. Aqui os fonemas vêm do próprio roteiro (espeak pt-BR, o mesmo
do Piper) e o Whisper (offline) só diz quando cada palavra começa e termina.

Rodar com o ambiente do Tools/VoiceGen (precisa de piper-tts e faster-whisper):

    ..\\VoiceGen\\.venv\\Scripts\\python visemas_texto.py                  # todas as falas de output/
    ..\\VoiceGen\\.venv\\Scripts\\python visemas_texto.py --only P2_nome_data

Grava output/<id>.visemas.txt no formato "inicio:forma;..." (formas do Rhubarb A-H e X, as mesmas
que o UPatientVoiceComponent já entende) e reescreve output/DT_FalasPaciente.csv.
"""

import argparse
import csv
import difflib
import os
import re
import subprocess
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VOICEGEN = ROOT.parent / "VoiceGen"
PHONEMIZER_VOICE = VOICEGEN / "models" / "pt_BR-faber-medium.onnx"
WHISPER_MODEL = "small"
MIN_GAP_S = 0.12          # pausa entre palavras que fecha a boca (X)

# Fonema do espeak pt-BR -> forma do Rhubarb
# A = M/B/P, B = dentes quase fechados (S, T, I...), C = E, D = A, E = O, F = U/W, G = F/V, H = L
SHAPES = {
    "a": "D", "ɐ": "C", "e": "C", "ɛ": "C", "i": "B", "ɪ": "B", "j": "B", "y": "B",
    "o": "E", "ɔ": "E", "u": "F", "ʊ": "F", "w": "F",
    "p": "A", "b": "A", "m": "A",
    "f": "G", "v": "G",
    "l": "H", "ʎ": "H", "ɫ": "F",
    "ʃ": "E", "ʒ": "E",
}
VOWELS = set("aɐeɛiɪoɔuʊy")
# Peso de duração relativa dentro da palavra (vogais duram mais que consoantes)
WEIGHT_VOWEL, WEIGHT_STRESSED, WEIGHT_CONSONANT = 1.6, 2.2, 1.0


def normalize(word: str) -> str:
    word = unicodedata.normalize("NFD", word.lower())
    return "".join(c for c in word if c.isalnum() and unicodedata.category(c) != "Mn")


def script_words(text: str) -> list[str]:
    return re.findall(r"[\wÀ-ÿ]+", text)


class Phonemizer:
    def __init__(self):
        from piper import PiperVoice
        if not PHONEMIZER_VOICE.exists():
            sys.exit(f"Voz Piper não encontrada em {PHONEMIZER_VOICE}; rode o gerar_vozes.py uma vez.")
        self.voice = PiperVoice.load(PHONEMIZER_VOICE)

    def word(self, word: str) -> list[tuple[str, bool]]:
        """Fonemas da palavra como (símbolo, tônico)."""
        phones, stressed = [], False
        for sentence in self.voice.phonemize(word):
            for ph in sentence:
                if ph in ("ˈ", "ˌ"):
                    stressed = ph == "ˈ"
                elif ph in ("̃", "ː", " ", "-") or not ph.strip():
                    continue
                elif ph.isalpha() or ph in SHAPES:
                    phones.append((ph, stressed and ph in VOWELS))
                    if ph in VOWELS:
                        stressed = False
        return phones


def load_16k(wav: Path):
    """WAV mono 16-bit -> float32 a 16 kHz (entrada do Whisper), sem depender do ffmpeg/PyAV."""
    import wave

    import numpy as np
    from scipy.signal import resample_poly
    with wave.open(str(wav), "rb") as w:
        rate = w.getframerate()
        samples = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
        if w.getnchannels() > 1:
            samples = samples.reshape(-1, w.getnchannels()).mean(axis=1)
    return resample_poly(samples, 16000, rate).astype(np.float32)


def whisper_words(model, wav: Path, text: str) -> list[tuple[str, float, float]]:
    # temperature=0: sem o "fallback" aleatório, que às vezes devolve tempos espremidos
    segments, _ = model.transcribe(load_16k(wav), language="pt", word_timestamps=True,
                                   initial_prompt=text or None, vad_filter=False, beam_size=5,
                                   temperature=0.0, condition_on_previous_text=False)
    return [(w.word.strip(), w.start, w.end) for s in segments for w in s.words]


def align(script: list[str], heard: list[tuple[str, float, float]], duration: float):
    """Tempo de cada palavra do roteiro: casa com as palavras ouvidas e interpola as que faltarem."""
    a = [normalize(w) for w in script]
    b = [normalize(w) for w, _, _ in heard]
    times: list[tuple[float, float] | None] = [None] * len(script)
    matcher = difflib.SequenceMatcher(a=a, b=b, autojunk=False)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                times[i1 + k] = heard[j1 + k][1:]
        elif tag == "replace" and j2 > j1:
            # Ex.: "2002" no roteiro e "dois mil e dois" ouvido: divide o trecho ouvido entre as palavras
            start, end = heard[j1][1], heard[j2 - 1][2]
            n = i2 - i1
            for k in range(n):
                times[i1 + k] = (start + (end - start) * k / n, start + (end - start) * (k + 1) / n)

    # Preenche as que não casaram entre as vizinhas conhecidas
    first = next((t[0] for t in times if t), 0.0)
    last = next((t[1] for t in reversed(times) if t), duration)
    i = 0
    while i < len(times):
        if times[i] is not None:
            i += 1
            continue
        j = i
        while j < len(times) and times[j] is None:
            j += 1
        start = times[i - 1][1] if i > 0 else first
        end = times[j][0] if j < len(times) else last
        n = j - i
        for k in range(n):
            times[i + k] = (start + (end - start) * k / n, start + (end - start) * (k + 1) / n)
        i = j
    return times


def visemes(phonemizer: Phonemizer, model, wav: Path, text: str, duration: float) -> str:
    words = script_words(text)

    def plausible(times) -> bool:
        # Palavras com duração de fala real e espalhadas pela maior parte do áudio
        spans = sorted(end - start for start, end in times)
        return spans[len(spans) // 2] >= 0.07 and times[-1][1] - times[0][0] >= 0.5 * duration

    times = align(words, whisper_words(model, wav, text), duration)
    if not plausible(times):
        # Sem o texto de referência o Whisper às vezes acerta onde com ele "espremeu" as palavras
        retry = align(words, whisper_words(model, wav, ""), duration)
        if plausible(retry):
            times = retry
        else:
            print(f"  aviso: tempos suspeitos em {wav.name}; confira a boca nessa fala")
    cues: list[tuple[float, str]] = [(0.0, "X")]
    prev_end = 0.0
    for word, (start, end) in zip(words, times):
        if start - prev_end > MIN_GAP_S:
            cues.append((prev_end, "X"))
        phones = phonemizer.word(word)
        if not phones:
            continue
        weights = [WEIGHT_STRESSED if s else WEIGHT_VOWEL if p in VOWELS else WEIGHT_CONSONANT for p, s in phones]
        t = start
        for (ph, _), w in zip(phones, weights):
            cues.append((t, SHAPES.get(ph, "B")))
            t += (end - start) * w / sum(weights)
        prev_end = end
    cues.append((prev_end, "X"))

    # Junta formas repetidas e descarta trocas mais curtas que um quadro (~16 ms)
    merged: list[tuple[float, str]] = []
    for t, shape in sorted(cues, key=lambda c: c[0]):
        if merged and (shape == merged[-1][1] or t - merged[-1][0] < 0.016):
            if t - merged[-1][0] < 0.016 and shape != merged[-1][1]:
                merged[-1] = (merged[-1][0], shape)
            continue
        merged.append((t, shape))
    return ";".join(f"{t:.2f}:{s}" for t, s in merged)


def wav_duration(path: Path) -> float:
    import wave
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / w.getframerate()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", type=Path, default=VOICEGEN / "falas_paciente.csv")
    parser.add_argument("--out", type=Path, default=VOICEGEN / "output")
    parser.add_argument("--only", nargs="*", help="Processa apenas estes ids")
    parser.add_argument("--sem-tabela", action="store_true", help="Não reescreve DT_FalasPaciente.csv")
    args = parser.parse_args()

    os.environ.setdefault("HF_HUB_DISABLE_XET", "1")   # o downloader Rust falha com pouca memória
    from faster_whisper import WhisperModel

    with args.csv.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    phonemizer = Phonemizer()
    model = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")

    for row in rows:
        if args.only and row["id"] not in args.only:
            continue
        wav = args.out / f"{row['id']}.wav"
        if not wav.exists():
            print(f"  {row['id']}: sem .wav, pulando")
            continue
        text = (row.get("texto_tts") or "").strip() or row["texto"].strip()
        result = visemes(phonemizer, model, wav, text, wav_duration(wav))
        (args.out / f"{row['id']}.visemas.txt").write_text(result, encoding="utf-8")
        print(f"  {row['id']}: {result.count(';') + 1} formas em {wav_duration(wav):.1f}s")

    if not args.sem_tabela:
        subprocess.run([sys.executable, str(VOICEGEN / "gerar_vozes.py"), "--so-tabela",
                        "--csv", str(args.csv), "--out", str(args.out)], check=True)


if __name__ == "__main__":
    main()
