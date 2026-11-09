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

### ✅ 3.7. Humanização Visual: Expressividade Facial, Piscada Anatômica, Olhar Vivo e Respiração
* **Diagnóstico e Correção da Piscada (`Blink`)**:
  * *Problema anterior*: A rotação linear dos vértices arrastava os cantos dos olhos (canthi medial e lateral) para dentro da maxila, causando a impressão de uma "pele esticada não-natural" caindo sobre a face.
  * *Solução matemática*: Ancoragem dos ligamentos cantais (movimento zero nas extremidades) com perfil senoidal/parabólico (`math.cos(u * pi * 0.5) ** 1.3`), preservando a calota esférica do globo ocular e sincronizando o rim dos cílios superiores para selar perfeitamente contra a pálpebra inferior sem deformações.
* **Olhar Vivo e Rastreamento Ocular (Rig Mixamo sem Bones de Olho)**:
  * *Problema anterior*: O rig do Mixamo não possui `LeftEye`/`RightEye` bones; o código C++ não conseguia girar os olhos, deixando os alunos com a sensação de um boneco estático com olhar congelado para a frente.
  * *Solução*: Criação de 4 novos morph targets esféricos (`EyeLookLeft`, `EyeLookRight`, `EyeLookUp`, `EyeLookDown`) no Blender 5.1 que rotacionam as ilhas dos globos oculares em seus centros ópticos.
  * *Integração C++*: `PatientAnimInstance` computa micro-sacadas oculares e rotação até o jogador, passando os valores normalizados para `PatientLookAtComponent`, que aciona os morphs dos olhos imediatamente antes da cabeça virar.
* **Expressões Faciais Vívidas em Escala de VR**:
  * Reformulação completa das amplitudes de expressão no Blender e C++ para legibilidade a 1,5–2 metros de distância no Meta Quest:
    * `Smile`: Cantos da boca elevados (+10mm), bochechas projetadas (+8mm Z, -4mm Y) e contração palpebral inferior Duchenne.
    * `BrowDown`: Franzimento do músculo corrugador (-8mm para a linha média, -10mm Z) com transição contínua sem descontinuidades no centro da glabela.
    * `BrowUp`: Elevação expressiva do arco superciliar e testa (+12.5mm Z) sincronizada com os picos da fala e saudações.
    * `Squint`: Contração palpebral para desconforto, dor ou foco.
* **Respiração Torácica Orgânica**:
  * Inclusão de oscilação respiratória senoidal assimétrica contínua (~0.24 Hz / 14 ciclos por minuto) em `Spine1` e `Spine2` via `PatientAnimInstance.cpp`, eliminando a rigidez do tronco sentado.
* **Seguidor de Envelope de Áudio Ativo**:
  * Configuração de `EnvelopeFollowerAttackTime = 20` e `EnvelopeFollowerReleaseTime = 100` no `UPatientVoiceComponent`, garantindo modulação realista da mandíbula em função da energia sonora.

---

## 4. 🧭 Planejamento e Próximos Passos

### 🔹 Continuidade dos Requisitos Clínicos (Passo 7 em diante)
* Implementação da sequência da intervenção prática de enfermagem a partir do Passo 7, após a finalização dos passos preparatórios e higienização das mãos (`BP_WaterHands_Step`, `BP_SoapHands_Step`).

### 🔹 Refinamento da Simulação em Realidade Virtual
* Teste em dispositivo físico (Meta Quest) das transições de fala e ajuste fino de `Viseme Time Offset` caso haja latência de hardware.
* Expansão da voz para a personagem da Enfermeira Simone caso necessário no acolhimento.
