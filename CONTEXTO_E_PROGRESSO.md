# 🩺 Projeto VIDA Enfermagem (MedicalGestureLib)
## Documento de Contexto Geral, Histórico e Progresso Alcançado

---

## 1. 📋 Visão Geral do Projeto
* **Repositório**: `VidaEnfermagemLib`
* **Arquivo de Projeto da Engine**: `MedicalGestureLib.uproject`
* **Versão da Engine**: **Unreal Engine 5.4**
* **Plataforma Alvo**: Meta Quest 2 / Quest 3 / Quest Pro (Android arm64) & Win64
* **Objetivo Geral**: Desenvolver uma experiência imersiva e interativa de simulação em Realidade Virtual para treinamento de procedimentos de enfermagem, utilizando rastreamento de mãos (*hand tracking*), interações gestuais e acompanhamento de etapas clínicas padronizadas.

---

## 2. ⚠️ Desafios Herdados e Estado Inicial
No início do ciclo de trabalho atual, após estudo do material e do TCC desenvolvido anteriormente, foram identificados gargalos críticos que impediam a continuidade dos requisitos clínicos:

1. **Queda Crítica de Desempenho (~30 FPS)**:
   * A inserção do modelo 3D do paciente e dos seus componentes na cena causava quedas severas na taxa de quadros, gerando desconforto visual e cinetose (*motion sickness*), inviabilizando o uso em óculos VR standalone.
2. **Assets e Animações Danificados na Migração**:
   * O modelo do paciente (*Remy*) perdeu referências de materiais, texturas e malhas esqueléticas.
   * Bug na animação de transição *Sentando $\to$ Sentado*, no qual o paciente era puxado para trás da cadeira.
   * Dependências antigas acopladas ao plugin do Leap Motion nos nós de Blueprint.
3. **Incompatibilidade de Plugins Legados**:
   * O projeto original dependia de ferramentas descontinuadas (`OculusHandTools`, `OculusUtils`) que quebravam o build na Unreal Engine 5.4.

---

## 3. 🚀 O que Foi Conquistado até o Momento

### ✅ 3.1. Otimização de Performance e Estabilidade (90 FPS)
* Otimização das malhas, nós e lógicas do paciente e do cenário.
* Estabilização da taxa de quadros em **90 FPS constantes** no Meta Quest, garantindo conformidade com os padrões de conforto e fluidez para RV.

### ✅ 3.2. Recuperação Completa do Paciente (Remy)
* Limpeza e substituição dos nós legados dependentes de hardware antigo (Leap Motion).
* Correção do bug de animação através de compensação e translação no eixo X na reimportação, alinhando a origem no plano cartesiano entre as poses *Sentando* e *Sentado*.
* Restauração íntegra de materiais, esqueletos e instâncias na cena.

### ✅ 3.3. Migração para MetaXR 5.4 e Limpeza de Código
* Remoção definitiva dos plugins legados incompatíveis (`OculusHandTools`, `OculusUtils`).
* Integração e configuração do plugin moderno oficial **MetaXR (v5.4)** com suporte pleno a OpenXR e componentes de interação do SDK da Meta (`ISDK Interactable Widget`).
* Manutenção e compilação de código nativo em C++ no módulo do projeto (`MedicalGestureLib`) e classes de suporte.

### ✅ 3.4. Pipeline de Empacotamento e Teste em Hardware (APK)
* Configuração e empacotamento bem-sucedido para Android ASTC (arm64).
* Geração do executável de instalação e testes: `project-apks/Android_ASTC/MedicalGestureLib-arm64.apk`.
* Scripts automatizados de instalação e remoção via ADB (`Install_MedicalGestureLib-arm64.bat` e `Uninstall_MedicalGestureLib-arm64.bat`).

### ✅ 3.5. Arquitetura de Histórias, Diálogos e Cenas
* **Cena Principal**: `Procedimento_SceneEvolution`, recuperando e unificando os recursos desenvolvidos e recuperados.
* **Sistema de Diálogos e Questões**:
  * `BP_Dialogue_Widget_Manager`: Gerenciamento do widget 3D interativo compatível com gestos e toques.
  * `BP_Dialogue_Step`: Controle de fluxo com dispatchers para respostas certas, erradas e término de fala.
  * `StoryBuilderWidget`: Interface no editor com as abas de *Anchors*, *StoryTeller* e *DialogueBuilder*.

### ✅ 3.6. Voz do Paciente com IA (Gemini 3.8 Flash TTS) e Lip Sync Anatômico (Rhubarb)
* **Humanização com Google AI Studio**:
  * Síntese neural das 18 falas dos Passos 0 a 6 via Gemini 3.8 Flash TTS (voz `Puck`), com timbre e entonação natural de um jovem brasileiro de 24 anos (José Carlos da Silva).
  * Conversão padronizada para `.wav` mono 16-bit 44.1 kHz, garantindo zero latência e preservando os 90 FPS no Meta Quest.
* **Sincronia Labial (Rhubarb Lip Sync + Morph Targets)**:
  * Criação de 7 morph targets faciais na malha `SK_Remy_Visemas` (`JawOpen`, `MouthPucker`, `MouthWide`, `LowerLipIn`, `LipsPress`, `UpperLipUp`, `TongueUp`).
  * Processamento automatizado dos arquivos de áudio pelo Rhubarb para extração de fonemas e visemas temporizados.
* **Componente Nativo C++ (`UPatientVoiceComponent`)**:
  * Emissão de áudio espacializado atrelado ao osso `Head` do Remy.
  * Animação labial suave em tempo real com coarticulação, modulação da mandíbula pelo volume e variação orgânica.
  * Mapeamento reativo automático com `BP_Quiz` através do nó `Patient React To Quiz Widget`, associando as escolhas do estudante às falas configuradas na `DT_FalasPaciente`.
* **Pipeline e Scripts Facilitadores**:
  * `gerar_vozes_gemini.bat`: Geração em lote via API do Google AI Studio com proteção de rate limit.
  * `importar_na_unreal.bat`: Importação direta de áudios, DataTable e configuração do `BP_Patient` via linha de comando (`configurar_paciente_ue.py`).
  * `ouvir_amostras.bat`: Painel comparativo interativo em HTML (`amostras.html`) para validação auditiva.

### ✅ 3.7. Humanização Visual: Expressividade Facial, Piscada Anatômica, Olhar Vivo, Respiração e Prosódia de Fala
* **Diagnóstico e Correção da Piscada (`Blink`)**:
  * *Problema anterior*: A rotação linear dos vértices arrastava os cantos dos olhos (canthi medial e lateral) para dentro da maxila, causando a impressão de uma "pele esticada não-natural" caindo sobre a face.
  * *Solução matemática*: Ancoragem dos ligamentos cantais (movimento zero nas extremidades) com perfil senoidal/parabólico (`math.cos(u * pi * 0.5) ** 1.3`), preservando a calota esférica do globo ocular e sincronizando o rim dos cílios superiores para selar perfeitamente contra a pálpebra inferior sem deformações.
* **Olhar Vivo e Rastreamento Ocular (Rig Mixamo sem Bones de Olho)**:
  * *Problema anterior*: O rig do Mixamo não possui `LeftEye`/`RightEye` bones; o código C++ não conseguia girar os olhos, deixando os alunos com a sensação de um boneco estático com olhar congelado para a frente.
  * *Solução*: Criação de 4 novos morph targets esféricos (`EyeLookLeft`, `EyeLookRight`, `EyeLookUp`, `EyeLookDown`) no Blender 5.1 que rotacionam as ilhas dos globos oculares em seus centros ópticos.
  * *Integração C++*: `PatientAnimInstance` computa micro-sacadas oculares e rotação até o jogador, passando os valores normalizados para `PatientLookAtComponent`, que aciona os morphs dos olhos imediatamente antes da cabeça virar.
* **Refino de Expressões Faciais Vívidas (Escultura Anatômica e Prosódia em C++)**:
  * *Amplitudes Anatômicas Refinadas (Blender 5.1)*:
    * `BrowUp`: Elevação expressiva expandida (+16.5mm) para ser claramente legível através da franja do cabelo de Remy.
    * `BrowDown`: Franzimento corrugador expandido (-14mm Z, -11mm medial) para conferir dramaticidade e tensão clínica realista.
* **Calibração Natural da Boca, Repouso Anatômico e Sincronia Labial Humana**:
  * *Eliminação do Efeito "Uncanny / Caricato"*:
    * No repouso, a boca agora permanece **100% selada, relaxada e anatômica** (`BaseSmile = 0.0f`), eliminando a fresta com dentes visíveis e a tensão horizontal de sorriso forçado. O centro dos lábios (filtro e arco do cupido) é preservado integralmente sem distorções.
    * O sorriso de saudação (`GreetingSmile = 0.40f`) atua nos cantos da boca com elevação sutil (4.0mm Z, 2.5mm X) e maçãs do rosto (3.0mm Z), transmitindo acolhimento humano educado por 2 segundos antes de retornar ao repouso sereno.
  * *Refinamento dos Visemas e Lip Sync no Rhubarb (Abertura Labial Balanceada)*:
    * Rotação da mandíbula (`JAW_ANGLE`): Calibrada em **12.0°** no Blender 5.1 (arco ideal para visualização nítida em VR sem aspecto de bocejo forçado).
    * `corner_anchor`: Os cantos da boca acompanham a mandíbula com ~45% de conformidade elástica, conferindo um formato oval orgânico e abertura limpa da cavidade oral durante a fala.
    * `UpperLipUp`: Mantido contido em 1.8mm max, impedindo que o lábio superior suba como um esgar ao pronunciar consoantes dentais/fricativas.
    * `MouthWide`: Mantido contido em 4.0mm max, evitando o estiramento horizontal exagerado.
    * `JawOpen` nos fonemas do Rhubarb calibrado para abertura clara e articulada das sílabas (Vogal "A" com 0.85; "E" e "O" com 0.50; consoantes de apoio entre 0.14 e 0.18; repouso "A/MBP" selado em 0.00).
    * Modulação de volume aprimorada (`Lerp(0.85f, 1.25f, Loudness)`), garantindo que sílabas de volume moderado nunca sejam abafadas e retenham ao menos 85% da abertura fonética real.
    * Adicionada a propriedade `JawScale` (default 1.0, editável de 0.0 a 2.5 no painel *Details* da Unreal Engine) para permitir sintonia fina da abertura labial em tempo real sem necessidade de recompilar.
    * Agilidade da mandíbula aumentada (`JawSmoothingSpeed = 16.0f`), garantindo que palavras curtas e sílabas rápidas abram e fechem com nitidez temporal.
  * *Expressividade e Olhar Vivo dos Olhos (Restauração do Ponto de Equilíbrio)*:
    * Elevação das sobrancelhas (`BrowUp`): Calibrada em **12.0mm** no Blender 5.1 (visível com clareza através da franja do cabelo, sem subir para a raiz). Teto conversacional em C++ expandido para **0.65 max** com acentuação por prosódia (`0.20 * SpeechActivity + SpeechBrowRaise * SpeechPeak`).
    * Franzimento / Concentração (`BrowDown`): Calibrado em **10.0mm**, transmitindo foco clínico acolhedor e atencioso ao escutar o aluno.
    * Contração palpebral e sorriso nos olhos (`Squint`): Calibrado em **6.0mm** no 3D e até **0.85 max** em C++, reativando o *Duchenne smile* (olhos que sorriem e se iluminam com a fala e acolhimento).
    * Micro-movimentos de atenção (`ThoughtWave`): Ondulação atenta contínua (0.15 lift) ao escutar em repouso, eliminando a rigidez estática.
  * *Micro-movimentos Orgânicos em Repouso / Escuta*:
    * Micro-respiração facial contínua ($\pm 0.03$ bochechas, $\pm 0.04$ sobrancelhas) sincronizada com o ciclo respiratório torácico.
    * Ondas cognitivas e de atenção a cada 3–5 segundos (`ThoughtWave`), variando levemente a postura das sobrancelhas e o olhar, eliminando o aspecto de estátua ou manequim quando em silêncio.
    * Linha de base acolhedora (`0.18 * Engagement`), mantendo o rosto relaxado e receptivo enquanto escuta o aluno.
  * *Prosódia e Expressividade Ativa Durante a Fala*:
    * Transição suave para o modo de conversação ativa (`SpeechActivity` via `FInterpTo`).
    * Flexão dinâmica das bochechas/zigomáticos com as sílabas e volume vocal (`0.30 * Speech + 0.15 * SpeechPeak`), evitando que apenas a mandíbula se mova como um boneco de ventríloquo.
    * Elevação conversacional das sobrancelhas para ênfase nas palavras (`0.35 * SpeechActivity + SpeechBrowRaise * SpeechPeak`), com relaxamento elástico do corrugador.
* **Respiração Torácica Orgânica**:
  * Inclusão de oscilação respiratória senoidal assimétrica contínua (~0.24 Hz / 14 ciclos por minuto) em `Spine1` e `Spine2` via `PatientAnimInstance.cpp`, eliminando a rigidez do tronco sentado.
* **Seguidor de Envelope de Áudio Ativo**:
  * Configuração de `EnvelopeFollowerAttackTime = 20` e `EnvelopeFollowerReleaseTime = 100` no `UPatientVoiceComponent`, garantindo modulação realista da mandíbula em função da energia sonora.

### ✅ 3.8. Auditoria Investigativa Biométrica FACS e Humanização Avançada do Remy
* **Estudo Investigativo Multi-Ângulo (Blender 5.1 & Render Headless)**:
  * Desenvolvimento de script automatizado (`investigar_remy.py`) gerando **80 capturas em alta resolução (800x800)** através de 5 câmeras clínicas (`aluno_vr`, `retrato_frontal`, `macro_olhos`, `macro_boca`, `perfil_clinico`) cobrindo 16 estados conversacionais, fonéticos e emocionais.
  * Mapeamento métrico tridimensional dos 16 morph targets pelo padrão internacional FACS (*Facial Action Coding System*), mensurando amplitudes milimétricas reais de deslocamento ($\Delta X, \Delta Y, \Delta Z$).
* **Erradicação dos 5 Fatores do Vale da Estranheza (*Uncanny Valley*)**:
  1. *Grau de Liberdade de Inclinação Lateral da Cabeça (Head Roll / 3-DOF)*: Implementação de rotação com componente de inclinação natural ao falar ($1.6^\circ$) e em repouso ($1.2^\circ$), distribuído entre pescoço (40%) e cabeça (60%) em `PatientAnimInstance.cpp`.
  2. *Micro-Assimetria Facial Orgânica*: Eliminação da simetria bilateral artificial do 3D através de variação de ~4% nas sobrancelhas (`BrowUp`) e nos cantos labiais (`Smile`).
  3. *Triângulo do Olhar Social de Argyle-Cook*: Substituição de ruído aleatório nas sacadas por ciclo de fixação foveal humana em 4 fases: olho esquerdo do aluno (1.8s–3.0s) $\to$ olho direito (1.8s–3.0s) $\to$ comissura da boca (1.0s–2.0s) $\to$ micro-desvio de pensamento (0.7s–1.4s).
  4. *Ativação do Músculo Mentoniano (AU 17 - Chin Raiser)*: Modelagem da contração cutânea do queixo (+1.2mm) durante o selamento labial oclusivo (`LipsPress` para 'M', 'B', 'P').
  5. *Coarticulação Sigmoidal Suave*: Aplicação de interpolação não-linear `FMath::SmoothStep` no `PatientVoiceComponent.cpp`, eliminando estalos visuais entre fonemas adjacentes.
* **Documentação Científica e Visual**:
  * Publicação do dossiê investigativo detalhado com carrosséis e medições milimétricas em `DOSSIE_INVESTIGATIVO_EXPRESSOES_PACIENTE.md`.

---

## 4. 🧭 Planejamento e Próximos Passos

### 🔹 Continuidade dos Requisitos Clínicos (Passo 7 em diante)
* Implementação da sequência da intervenção prática de enfermagem a partir do Passo 7, após a finalização dos passos preparatórios e higienização das mãos (`BP_WaterHands_Step`, `BP_SoapHands_Step`).

### 🔹 Refinamento da Simulação em Realidade Virtual
* Teste em dispositivo físico (Meta Quest) das transições de fala e ajuste fino de `Viseme Time Offset` caso haja latência de hardware.
* Expansão da voz para a personagem da Enfermeira Simone caso necessário no acolhimento.
