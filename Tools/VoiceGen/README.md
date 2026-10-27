# Voz do Paciente (Piper TTS + UPatientVoiceComponent)

Falas do paciente geradas offline com Piper (pt-BR, voz masculina `pt_BR-faber-medium`) e tocadas
na cabeça do Remy com áudio espacial e abertura de boca baseada no volume do áudio.

## 1. Editar as falas

`falas_paciente.csv`:

| coluna      | uso                                                                 |
|-------------|---------------------------------------------------------------------|
| `id`        | nome da linha no DataTable e do arquivo `.wav` (ex.: `P2_nome_data`) |
| `passo`     | passo clínico (0 = antes do procedimento)                           |
| `gatilho`   | quando a fala deve tocar (documentação)                             |
| `texto`     | legenda exibida                                                      |
| `texto_tts` | opcional: texto falado, quando difere da legenda (números por extenso) |

Textos com vírgula precisam ficar entre aspas.

## 2. Gerar os áudios

Primeira vez:

```bash
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
```

Gerar:

```bash
.venv\Scripts\python gerar_vozes.py
```

Opções: `--only P2_nome_data P5_consentimento`, `--voice pt_BR-cadu-medium`, `--speed 0.95`.
A voz é baixada para `models/` na primeira execução. Saída em `output/`:
WAVs mono 16-bit 44.1 kHz e `DT_FalasPaciente.csv`.

## 3. Importar na Unreal

1. Arraste os `.wav` de `output/` para `Content/Audio/Paciente` (o caminho precisa bater com
   `--ue-path`, padrão `/Game/Audio/Paciente`).
2. Arraste `DT_FalasPaciente.csv` para `Content/Audio/Paciente` e escolha a estrutura
   **PatientVoiceLine** como Row Type. Para atualizar depois: botão direito no DataTable > Reimport.
3. Crie um **Sound Attenuation** (`ATT_VozPaciente`): Inner Radius ~50, Falloff ~600,
   Spatialization = Binaural (ou o spatializer do Meta XR Audio, se o plugin for instalado).

## 4. Configurar o paciente

No blueprint do paciente (ex.: `BP_Remy`), **Add Component > Patient Voice**:

- `Voice Lines`: `DT_FalasPaciente`
- `Attenuation`: `ATT_VozPaciente`
- `Head Bone Name`: `Head` (a voz sai desse osso)
- `Mouth Morph Target`: vazio por enquanto (veja a seção 6)

## 5. Usar nos Steps

Em `BP_Dialogue_Step` (ou nos steps de cada passo):

- **Speak Line** (`LineId` = `P2_nome_data`) quando o estudante escolher a opção.
- **On Speech Started** (`LineId`, `Texto`): mostrar a legenda no balão.
- **On Speech Finished** (`LineId`): só então liberar as próximas opções ou chamar `OnStepFinished`.

Sugestão de mapeamento:

| Momento                                    | Linha                         |
|--------------------------------------------|-------------------------------|
| Interagir sem higienizar as mãos           | `P0_maos_sujas`               |
| Após a apresentação da enfermeira          | `P1_acolhimento`              |
| Identificação: correta / sugestiva / só nome / pulseira | `P2_nome_data` / `P2_pergunta_sugestiva` / `P2_apenas_nome` / `P2_pulseira` |
| Alergias: completa / genérica / só álcool / procedimento padrão | `P3_alergia_completa` / `P3_alergia_generica` / `P3_alergia_alcool` / `P3_procedimento_padrao` |
| Coleta a vácuo / após explicação           | `P4_coleta_vacuo` / `P4_explicacao_ok` |
| Consentimento / sem consentimento          | `P5_consentimento` / `P5_sem_consentimento` |
| Refeição / repouso / medicamentos / sem verificação | `P6_ultima_refeicao` / `P6_repouso` / `P6_medicamentos` / `P6_sem_verificacao` |

## 6. Lip sync

O componente expõe `Mouth Open` (0..1, suavizado) a partir do envelope do áudio.

O `SK_Remy` (rig Mixamo) **não tem osso de mandíbula nem morph targets**, então a boca não abre sem
alterar o modelo. Opções:

- **Agora, sem mexer no modelo:** no AnimBP do paciente, leia `Mouth Open` do componente e use um
  nó *Transform (Modify) Bone* no osso `Head` com uma rotação pequena (2–4°) multiplicada pelo valor.
  Dá vida à fala sem custo de performance.
- **Lip sync real:** no Blender, adicione um morph target `MouthOpen` (ou um osso `Jaw` pesado no
  queixo), reimporte o `SK_Remy` com *Import Morph Targets* e preencha `Mouth Morph Target` no componente.

## Performance (Quest)

- `bPreloadLines` carrega todas as falas no BeginPlay para não travar no meio do diálogo.
- O tick do componente só fica ligado enquanto o paciente fala.
- Nos `.wav` importados, mantenha Loading Behavior padrão e compressão ADPCM/Vorbis com qualidade ~70.
