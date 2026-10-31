"""Gera amostras comparativas de voz para avaliação auditiva da qualidade."""
import asyncio
import io
import wave
from pathlib import Path
import numpy as np
from piper import PiperVoice, SynthesisConfig
import miniaudio
import edge_tts

ROOT = Path(__file__).resolve().parent
MODELS = ROOT / "models"
OUT = ROOT / "amostras"
OUT.mkdir(exist_ok=True)

TEXTO = "Bom dia! Ah, eu tô bem, sim... só um pouquinho ansioso com o exame, sabe?"
TARGET_RATE = 44100

def write_wav(path, samples, rate=TARGET_RATE):
    with wave.open(str(path), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(samples.tobytes() if isinstance(samples, np.ndarray) else samples)

def synth_piper(model_name, speed=0.95, noise=0.667, noise_w=0.8):
    model = MODELS / f"{model_name}.onnx"
    voice = PiperVoice.load(model)
    config = SynthesisConfig(length_scale=1.0 / speed, noise_scale=noise, noise_w_scale=noise_w)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as f:
        voice.synthesize_wav(TEXTO, f, syn_config=config)
    buf.seek(0)
    with wave.open(buf, "rb") as f:
        src_rate = f.getframerate()
        raw = f.readframes(f.getnframes())
    samples = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    # resample
    duration = len(samples) / src_rate
    src_t = np.arange(len(samples)) / src_rate
    dst_t = np.arange(int(round(duration * TARGET_RATE))) / TARGET_RATE
    samples = np.interp(dst_t, src_t, samples).astype(np.float32)
    peak = float(np.max(np.abs(samples))) or 1.0
    samples = samples * (10 ** (-3.0 / 20) / peak)
    return (np.clip(samples, -1.0, 1.0) * 32767).astype(np.int16)

async def synth_edge(voice_name, rate_str="+0%"):
    communicate = edge_tts.Communicate(TEXTO, voice_name, rate=rate_str)
    mp3_data = io.BytesIO()
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            mp3_data.write(chunk["data"])
    mp3_data.seek(0)
    decoded = miniaudio.decode(mp3_data.read(), nchannels=1, sample_rate=TARGET_RATE, output_format=miniaudio.SampleFormat.SIGNED16)
    return decoded.samples

async def main():
    print("1. Gerando Piper - Jeff (Atual do projeto)...")
    from gerar_vozes import render, PiperVoice, SynthesisConfig
    voice_jeff = PiperVoice.load(MODELS / "pt_BR-jeff-medium.onnx")
    cfg_jeff = SynthesisConfig(length_scale=1.0 / 0.95, noise_scale=0.45, noise_w_scale=0.55)
    write_wav(OUT / "1_piper_jeff_atual.wav", render(voice_jeff, TEXTO, cfg_jeff, eq=True))

    print("2. Gerando Piper - Jeff (Sem equalizador e entonacao natural)...")
    write_wav(OUT / "2_piper_jeff_sem_eq.wav", synth_piper("pt_BR-jeff-medium", speed=0.95, noise=0.667, noise_w=0.8))

    print("3. Gerando Piper - Faber (Voz mais grave)...")
    write_wav(OUT / "3_piper_faber.wav", synth_piper("pt_BR-faber-medium", speed=0.95))

    print("4. Gerando Piper - Cadu (Voz mais jovem)...")
    write_wav(OUT / "4_piper_cadu.wav", synth_piper("pt_BR-cadu-medium", speed=0.95))

    print("5. Gerando Edge-TTS - Antonio Neural (Voz humana ultra-realista)...")
    data_antonio = await synth_edge("pt-BR-AntonioNeural")
    write_wav(OUT / "5_edge_antonio_neural_alta_qualidade.wav", data_antonio)

    print("6. Gerando Edge-TTS - Francisca Neural (Feminina ultra-realista)...")
    data_francisca = await synth_edge("pt-BR-FranciscaNeural")
    write_wav(OUT / "6_edge_francisca_neural_alta_qualidade.wav", data_francisca)

    print(f"Sucesso! Todas as amostras foram gravadas em: {OUT}")

if __name__ == "__main__":
    asyncio.run(main())

