# Voz do Paciente (Google AI Studio Gemini TTS / Piper / UPatientVoiceComponent)

Falas do paciente com voz jovem e natural (jovem de 24 anos) geradas via Google AI Studio (`gemini-3.8-flash-tts`, voz `Puck`), com visemas de sincronia labial calculados pelo Rhubarb e tocadas na cabeça do Remy com áudio espacial e abertura de boca anatômica.

## 1. Editar as falas

`falas_paciente.csv`:

| coluna      | uso                                                                 |
|-------------|---------------------------------------------------------------------|
| `id`        | nome da linha no DataTable e do arquivo `.wav` (ex.: `P2_nome_data`) |
| `passo`     | passo clínico (0 = antes do procedimento)                           |
| `gatilho`   | quando a fala deve tocar (documentação)                             |
| `texto`     | legenda exibida                                                      |
| `texto_tts` | opcional: texto falado, quando difere da legenda (números por extenso) |
| `disparo`   | texto exato da pergunta ou alternativa do quiz que faz o paciente dizer esta fala |

## 2. Gerar os áudios

### Opção A (Recomendada - Google AI Studio / Gemini 3.8 Flash TTS):
Basta dar duplo clique em `gerar_vozes_gemini.bat` dentro de `Tools/VoiceGen/`.
Ele usa a chave `AI_STUDIO_KEY` do `.env` e gera os 18 áudios com entonação humana jovem e os visemas do Rhubarb.

### Opção B (Edge-TTS ou Piper Offline):
- `gerar_vozes_edge.bat`: voz neural via Edge-TTS.
- `gerar_vozes.bat`: voz local offline via Piper TTS.

## 3. Importar e configurar na Unreal (automático)

Com o editor **fechado**:
Dê duplo clique em `importar_na_unreal.bat`.

O script:
1. Importa os `.wav` de `output/` para `/Game/Audio/Paciente`.
2. Importa `DT_FalasPaciente.csv` como DataTable com Row Type **PatientVoiceLine**.
3. No `BP_Patient`: troca a malha por `SK_Remy_Visemas` e adiciona o componente **PatientVoice** apontando para `DT_FalasPaciente`.

O script:

1. Importa os `.wav` de `output/` para `/Game/Audio/Paciente`.
2. Importa `DT_FalasPaciente.csv` como DataTable com Row Type **PatientVoiceLine**.
3. No `BP_Patient`: troca a malha do componente `remy` por `SK_Remy_Visemas` e adiciona o
   componente **PatientVoice** com `Voice Lines` = `DT_FalasPaciente`.

Pode ser rodado de novo depois de regenerar as falas: ele reimporta tudo e reaproveita o componente.

Sem `Attenuation` definido, o componente usa uma atenuação esférica (raio interno 150 cm,
`Falloff Distance` 1200 cm). Para usar o spatializer do Meta XR Audio, crie um Sound Attenuation
e coloque em `Attenuation`.

## 4. Ligar ao quiz (BP_Quiz)

O nó **Patient React To Quiz Widget** lê os textos de um widget do quiz e faz o paciente dizer a
fala cujo `disparo` bate com eles. Se ele já estiver falando, a fala entra na fila. São dois nós
no `Content/Blueprints/Interface/BP_Quiz`:

- No evento **OnQuizAnswerSelected**: ligue o pino `AnswerWidget` no nó (resposta do paciente à
  alternativa escolhida).
- No fim da função **ShowQuestion**: ligue a variável `CurrentQuestionWidget` no nó (falas que o
  paciente diz assim que a pergunta aparece, como `P4_coleta_vacuo`).

Para tocar uma fala por outro caminho (ex.: `P0_maos_sujas` na higienização), use **Speak Line**
com o `LineId`. **On Speech Started** (`LineId`, `Texto`) e **On Speech Finished** (`LineId`)
servem para legenda e para liberar a próxima etapa.

Mapeamento atual:

| Momento                                    | Linha                         |
|--------------------------------------------|-------------------------------|
| Interagir sem higienizar as mãos           | `P0_maos_sujas`               |
| Após a apresentação da enfermeira          | `P1_acolhimento`              |
| Identificação: correta / sugestiva / só nome / pulseira | `P2_nome_data` / `P2_pergunta_sugestiva` / `P2_apenas_nome` / `P2_pulseira` |
| Alergias: completa / genérica / só álcool / procedimento padrão | `P3_alergia_completa` / `P3_alergia_generica` / `P3_alergia_alcool` / `P3_procedimento_padrao` |
| Coleta a vácuo / após explicação           | `P4_coleta_vacuo` / `P4_explicacao_ok` |
| Consentimento / sem consentimento          | `P5_consentimento` / `P5_sem_consentimento` |
| Refeição / repouso / medicamentos / sem verificação | `P6_ultima_refeicao` / `P6_repouso` / `P6_medicamentos` / `P6_sem_verificacao` |

`P0_maos_sujas` e `P5_sem_consentimento` não têm `disparo`: não há alternativa do quiz que
corresponda a elas.

## 5. Lip sync

Com o Rhubarb Lip Sync em `Tools/LipSync/rhubarb` (veja `Tools/LipSync/README.md`), o gerador
grava a coluna `Visemas` no DataTable e o componente anima os morph targets `JawOpen`,
`MouthPucker`, `MouthWide` e `LowerLipIn` do `SK_Remy_Visemas` no tempo de cada fonema.

Sem visemas (Rhubarb ausente ou `SpeakSound` com som avulso), a boca segue o volume do áudio
apenas com `JawOpen`. O valor `Mouth Open` (0..1) continua exposto para uso no AnimBP.

Ajuste no Quest: se a boca estiver adiantada em relação à voz, aumente `Viseme Time Offset`
(0,05 a 0,1 s); se o movimento estiver forte ou fraco demais, ajuste `Mouth Intensity`.

## Performance (Quest)

- `bPreloadLines` carrega todas as falas no BeginPlay para não travar no meio do diálogo.
- O tick do componente só fica ligado enquanto o paciente fala.
- Nos `.wav` importados, mantenha Loading Behavior padrão e compressão ADPCM/Vorbis com qualidade ~70.
