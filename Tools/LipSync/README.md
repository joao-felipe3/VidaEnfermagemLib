# Lip Sync do Paciente (Remy)

O `SK_Remy` (rig Mixamo) não tem osso de mandíbula nem morph targets. Este fluxo cria formas de
boca no próprio modelo e sincroniza com as falas geradas em `Tools/VoiceGen`.

## Peças

| Peça | O que faz |
|------|-----------|
| `criar_visemas_remy.py` | Script do Blender que adiciona 7 morph targets ao Remy |
| `importar_visemas_ue.py` | Reimporta o FBX no `SK_Remy_Visemas` mantendo materiais e esqueleto |
| `rhubarb/` | Rhubarb Lip Sync 1.14.0 (MIT, offline). Não vai para o Git |
| `Tools/VoiceGen/gerar_vozes.py` | Roda o Rhubarb em cada `.wav` e grava a coluna `Visemas` |
| `UPatientVoiceComponent` | Converte os visemas em pesos dos morph targets durante a fala |
| `Content/RefAssets/Remy/SK_Remy_Visemas` | Remy com as formas de boca (mesmo esqueleto do `SK_Remy`) |

## Morph targets

| Nome | Uso |
|------|-----|
| `JawOpen` | Abre a mandíbula (16°): pele, lábio inferior, dentes, gengiva de baixo e língua |
| `MouthPucker` | Bico (O, U) |
| `MouthWide` | Lábios esticados (I, E, S) |
| `LowerLipIn` | Lábio inferior sob os dentes de cima (F, V) |
| `LipsPress` | Lábios fechados e pressionados (M, B, P) |
| `UpperLipUp` | Lábio superior sobe e mostra os dentes (S, E, F) |
| `TongueUp` | Ponta da língua atrás dos dentes de cima (L, T, D, N) |

O corte entre o lábio superior (fixo) e o inferior (acompanha o queixo) é feito pela distância
ao longo da pele até o nariz e até o queixo, porque os lábios do Remy se sobrepõem em altura.
Dentro da boca, cada dente é uma ilha da malha; as ilhas grandes (gengiva e céu da boca, língua,
gengiva de baixo) são separadas pela altura do centróide.

## Formas do Rhubarb para morph targets

| Forma | Som | Jaw | Pucker | Wide | LipIn | Press | UpperUp | Tongue |
|-------|-----|-----|--------|------|-------|-------|---------|--------|
| A | M, B, P | 0 | 0 | 0 | 0 | 1 | 0 | 0 |
| B | K, S, T, I | 0.10 | 0 | 0.35 | 0 | 0 | 0.45 | 0.20 |
| C | E | 0.35 | 0 | 0.30 | 0 | 0 | 0.25 | 0 |
| D | A | 0.75 | 0 | 0.10 | 0 | 0 | 0.10 | 0 |
| E | O | 0.35 | 0.50 | 0 | 0 | 0 | 0 | 0 |
| F | U, W | 0.12 | 0.95 | 0 | 0 | 0 | 0 | 0 |
| G | F, V | 0.06 | 0 | 0.10 | 0.90 | 0 | 0.35 | 0 |
| H | L | 0.35 | 0 | 0.10 | 0 | 0 | 0.10 | 1 |
| X | repouso | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

Os pesos ficam em `UPatientVoiceComponent::GetShapeWeights`. Durante a fala o componente ainda:

- antecipa o próximo visema (`Coarticulation Time`), como a boca real faz;
- abre mais a mandíbula nas sílabas mais fortes, pelo volume do áudio (`Envelope Influence`);
- varia levemente a força de cada visema (`Shape Variation`), para não parecer mecânico;
- move a mandíbula mais devagar que os lábios (`Jaw Smoothing Speed` / `Smoothing Speed`).

## Instalar o Rhubarb

Baixe `Rhubarb-Lip-Sync-1.14.0-Windows.zip` de
https://github.com/DanielSWolf/rhubarb-lip-sync/releases e extraia em `Tools/LipSync/rhubarb/`.
O `gerar_vozes.py` encontra o `rhubarb.exe` automaticamente.

## Recriar o SK_Remy_Visemas

1. Exporte `SK_Remy` da Unreal para FBX (botão direito > Asset Actions > Export).
2. Rode:

```bash
"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" -b --factory-startup --python criar_visemas_remy.py -- SK_Remy.fbx SK_Remy_Visemas.fbx previews
```

3. Com o editor fechado, reimporte (mantém esqueleto, materiais e physics asset):

```bash
"C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%cd%\MedicalGestureLib.uproject" -run=pythonscript -script="%cd%\Tools\LipSync\importar_visemas_ue.py %cd%\SK_Remy_Visemas.fbx" -unattended -nullrhi
```

Os previews (`previews/frente_*.png`, `previews/lado_*.png`) mostram cada forma para conferência.
As coordenadas do rosto no topo do script valem para o Remy; outro modelo exige medir de novo.

## Usar na cena

1. No paciente, troque a Skeletal Mesh `SK_Remy` por `SK_Remy_Visemas`. O esqueleto é o mesmo,
   então AnimBP e animações continuam funcionando.
2. Reimporte o `DT_FalasPaciente` com a coluna `Visemas` nova.
3. No componente **Patient Voice**, os nomes dos morph targets já vêm preenchidos. Ajuste:
   - `Mouth Intensity`: força geral da boca (comece em 1.0).
   - `Viseme Time Offset`: se no Quest a boca estiver adiantada em relação à voz, use ~0.05–0.1.

Custo: 7 morph targets em um único mesh, atualizados só enquanto o paciente fala.
