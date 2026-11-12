import math

# Simula a lógica de Normalização Inteligente / AGC
def normalize_pcm(samples):
    max_val = max(abs(s) for s in samples) if samples else 0
    if max_val == 0:
        return samples, 1.0
    
    # Alvo: pico em ~24000 (75% da escala de 32767, evitando clipping)
    # Limita o ganho máximo a 15x para não explodir ruído de fundo se for silêncio absoluto
    gain = min(24000.0 / max_val, 15.0)
    if gain < 1.0:
        gain = 1.0 # Nunca reduz se já estiver alto
        
    normalized = [int(max(-32768, min(32767, s * gain))) for s in samples]
    return normalized, gain

print("AGC logic ready.")

