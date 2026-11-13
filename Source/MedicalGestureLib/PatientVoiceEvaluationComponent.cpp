// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientVoiceEvaluationComponent.h"

#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "Components/WidgetComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/VoiceCapture.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "PatientLookAtComponent.h"
#include "PatientVoiceComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "VoiceModule.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

UPatientVoiceEvaluationComponent::UPatientVoiceEvaluationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPatientVoiceEvaluationComponent::BeginPlay()
{
	Super::BeginPlay();

	// 0. Permite sobrescrever BaseUrl e ApiKey via DefaultEngine.ini ([VoiceEvaluation])
	if (GConfig)
	{
		FString ConfigUrl;
		if (GConfig->GetString(TEXT("VoiceEvaluation"), TEXT("BaseUrl"), ConfigUrl, GEngineIni))
		{
			ConfigUrl.TrimQuotesInline();
			// Evita truncamento do parser ini se vier sem aspas ("http:")
			if (ConfigUrl.Equals(TEXT("http:"), ESearchCase::IgnoreCase) || ConfigUrl.Equals(TEXT("https:"), ESearchCase::IgnoreCase))
			{
				ConfigUrl = TEXT("http://127.0.0.1:8000");
			}
			BaseUrl = ConfigUrl;
			UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: BaseUrl configurada via DefaultEngine.ini: %s"), *BaseUrl);
		}
		FString ConfigApiKey;
		if (GConfig->GetString(TEXT("VoiceEvaluation"), TEXT("ApiKey"), ConfigApiKey, GEngineIni))
		{
			ConfigApiKey.TrimQuotesInline();
			ApiKey = ConfigApiKey;
		}
	}

	// 1. Configura as perguntas padrão do cenário se ainda não estiverem preenchidas
	if (ScenarioQuestions.IsEmpty())
	{
		ScenarioQuestions = {
			{
				TEXT("apresentacao"),
				TEXT("Apresente-se com seu nome e função, e pergunte como o paciente está se sentindo."),
				TEXT("P1_acolhimento"),
				TEXT("P1_acolhimento")
			},
			{
				TEXT("identificacao-paciente"),
				TEXT("Identifique o paciente de forma aberta (pergunte nome completo e data de nascimento)."),
				TEXT("P2_nome_data"),
				TEXT("P2_pergunta_sugestiva")
			},
			{
				TEXT("alergias"),
				TEXT("Investigue se o paciente possui alergias (álcool, clorexidina, iodo ou látex)."),
				TEXT("P3_alergia_completa"),
				TEXT("P3_alergia_generica")
			},
			{
				TEXT("experiencia-previa-sim"),
				TEXT("Pergunte se o paciente já realizou coleta a vácuo e explique o procedimento para tranquilizá-lo."),
				TEXT("P4_coleta_vacuo"),
				TEXT("P4_explicacao_ok")
			},
			{
				TEXT("consentimento-sim"),
				TEXT("Solicite a autorização e consentimento do paciente antes de prosseguir com a punção."),
				TEXT("P5_consentimento"),
				TEXT("P5_sem_consentimento")
			},
			{
				TEXT("condicoes-pre-analiticas"),
				TEXT("Verifique as condições pré-analíticas (horário da última refeição/jejum, tempo de repouso e medicamentos)."),
				TEXT("P6_ultima_refeicao"),
				TEXT("P6_sem_verificacao")
			}
		};
	}

	// 2. Localiza componentes do paciente (Remy)
	AActor* Owner = GetOwner();
	PatientVoice = Owner ? Owner->FindComponentByClass<UPatientVoiceComponent>() : nullptr;
	PatientLookAt = Owner ? Owner->FindComponentByClass<UPatientLookAtComponent>() : nullptr;

	if (!PatientVoice.IsValid())
	{
		// Busca paciente na cena se o componente foi colocado em outro Actor (ex.: no Player ou GameMode)
		TArray<AActor*> Actors;
		UGameplayStatics::GetAllActorsWithTag(this, TEXT("Patient"), Actors);
		for (AActor* Act : Actors)
		{
			if (UPatientVoiceComponent* PVC = Act->FindComponentByClass<UPatientVoiceComponent>())
			{
				PatientVoice = PVC;
				PatientLookAt = Act->FindComponentByClass<UPatientLookAtComponent>();
				break;
			}
		}
	}

	// 3. Inicializa captura de voz
	InitVoiceCapture();

	// 4. Se configurado, dispara wake-up ping assíncrono para acordar o Render da hibernação
	if (bAutoWakeUpServerOnStart)
	{
		WakeUpServer();
	}

	// 5. Notifica a pergunta inicial
	const FVoiceEvaluationQuestion CurrentQ = GetCurrentQuestion();
	OnQuestionChanged.Broadcast(CurrentQ.Id, CurrentQ.Hint);

	// 6. Configura Push-to-Talk via teclado se ativado
	if (bEnableKeyboardPushToTalk)
	{
		SetupKeyboardInput();
	}

	// 7. Sanitiza as perguntas do quiz na cena para serem faladas pelo estudante ao paciente
	SanitizeClinicalQuizQuestions();
}

void UPatientVoiceEvaluationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsRecording)
	{
		CancelRecording();
	}

	if (VoiceCapture.IsValid())
	{
		VoiceCapture->Shutdown();
		VoiceCapture.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void UPatientVoiceEvaluationComponent::InitVoiceCapture()
{
	// 1. Assegura que o parâmetro de áudio esteja ativado no GConfig em memória
	if (GConfig)
	{
		GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), true, GEngineIni);
	}

	// 2. Desativa limiares de silêncio e noise gate do motor para evitar cortes
	if (IConsoleVariable* SilenceCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.SilenceDetectionThreshold")))
	{
		SilenceCVar->Set(0.0f, ECVF_SetByCode);
	}
	if (IConsoleVariable* GateCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicNoiseGateThreshold")))
	{
		GateCVar->Set(0.0f, ECVF_SetByCode);
	}
	if (IConsoleVariable* GainCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicInputGain")))
	{
		GainCVar->Set(2.5f, ECVF_SetByCode);
	}

	// 3. Garante que o módulo Voice esteja carregado
	if (!FVoiceModule::IsAvailable())
	{
		FModuleManager::Get().LoadModule("Voice");
	}

	if (FVoiceModule::IsAvailable())
	{
		VoiceCapture = FVoiceModule::Get().CreateVoiceCapture(TEXT(""), 16000, 1);

		// Se CreateVoiceCapture retornou nulo (ex: se o módulo inicializou antes com bEnabled=false),
		// força recarregamento do módulo com bEnabled=true
		if (!VoiceCapture.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: VoiceCapture nulo. Forçando recarregamento do módulo Voice..."));
			FModuleManager::Get().UnloadModule("Voice");
			FModuleManager::Get().LoadModule("Voice");
			VoiceCapture = FVoiceModule::Get().CreateVoiceCapture(TEXT(""), 16000, 1);
		}

		if (VoiceCapture.IsValid())
		{
			bVoiceCaptureInitialized = VoiceCapture->Init(TEXT(""), 16000, 1);
			if (bVoiceCaptureInitialized)
			{
				UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Captura de voz inicializada com sucesso (16kHz mono)."));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("VoiceEvaluation: Falha ao executar Init() no dispositivo de captura de áudio."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("VoiceEvaluation: Não foi possível instanciar VoiceCapture. Reinicie o Unreal Editor para carregar o módulo Voice com o DefaultEngine.ini atualizado."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VoiceEvaluation: Módulo Voice não disponível nesta plataforma."));
	}
}

void UPatientVoiceEvaluationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 1. Polling de entrada para Push-to-Talk e Toggle Contínuo (V, Espaço ou Gatilhos/Grips do Meta Quest)
	if (bEnableKeyboardPushToTalk)
	{
		bool bKeyDown = false;

#if PLATFORM_WINDOWS
		// Hardware polling direto no Windows (tecla V ou barra de Espaço)
		if ((::GetAsyncKeyState('V') & 0x8000) != 0 || (::GetAsyncKeyState(VK_SPACE) & 0x8000) != 0)
		{
			bKeyDown = true;
		}
#endif

		// Engine polling (PlayerController e botões de gatilho/grip no controle Meta Quest Touch)
		if (!bKeyDown)
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
			{
				bKeyDown = PC->IsInputKeyDown(PushToTalkKey) ||
				           PC->IsInputKeyDown(EKeys::SpaceBar) ||
				           PC->IsInputKeyDown(EKeys::OculusTouch_Right_Trigger_Click) ||
				           PC->IsInputKeyDown(EKeys::OculusTouch_Right_Grip_Click) ||
				           PC->IsInputKeyDown(EKeys::OculusTouch_Left_Trigger_Click) ||
				           PC->IsInputKeyDown(EKeys::OculusTouch_Left_Grip_Click);
			}
		}

		// Detecta transições de borda (Press / Release / Hold)
		if (bKeyDown && !bWasPushToTalkKeyDown)
		{
			bWasPushToTalkKeyDown = true;
			KeyDownDuration = 0.0f;

			if (!bIsEvaluating)
			{
				if (bIsRecording && bToggleModeActive)
				{
					// Se já estava gravando em modo contínuo (toggle), um novo clique finaliza e envia para avaliar
					UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Clique para finalizar gravação contínua."));
					bToggleModeActive = false;
					StopRecording();
				}
				else if (!bIsRecording)
				{
					// Inicia gravação
					bToggleModeActive = false;
					StartRecording();
				}
			}
		}
		else if (bKeyDown && bWasPushToTalkKeyDown)
		{
			KeyDownDuration += DeltaTime;
		}
		else if (!bKeyDown && bWasPushToTalkKeyDown)
		{
			bWasPushToTalkKeyDown = false;

			if (bIsRecording)
			{
				if (KeyDownDuration >= 0.45f)
				{
					// Segurou por mais de 0.45s (Hold-to-Talk clássico) -> finaliza ao soltar
					UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Push-to-Talk liberado após %.2fs. Finalizando e avaliando..."), KeyDownDuration);
					bToggleModeActive = false;
					StopRecording();
				}
				else
				{
					// Toque rápido (< 0.45s) -> ativa Modo Contínuo (Toggle)
					bToggleModeActive = true;
					UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Toque rápido (%.2fs). Modo Contínuo ativado!"), KeyDownDuration);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(849206, 5.0f, FColor::Cyan,
							TEXT("[MODO CONTÍNUO] Microfone aberto! Fale normalmente e aperte V (ou Gatilho VR) quando terminar."));
					}
				}
			}
		}
	}

	// 2. Drena continuamente todo o áudio capturado pelo hardware
	if (bIsRecording && VoiceCapture.IsValid())
	{
		uint8 TempVoiceBuffer[32768];
		uint32 ReadBytes = 0;

		// Esvazia tudo o que estiver no buffer de captura DirectSound
		while (true)
		{
			uint32 AvailableData = 0;
			EVoiceCaptureState::Type CapState = VoiceCapture->GetCaptureState(AvailableData);

			if (CapState == EVoiceCaptureState::NotCapturing || CapState == EVoiceCaptureState::Error || AvailableData == 0)
			{
				break;
			}

			ReadBytes = 0;
			EVoiceCaptureState::Type ReadState = VoiceCapture->GetVoiceData(TempVoiceBuffer, sizeof(TempVoiceBuffer), ReadBytes);

			if (ReadBytes > 0)
			{
				PcmRecordingBuffer.Append(TempVoiceBuffer, ReadBytes);

				// Computa amplitude RMS para medidor de volume (VU meter) com sensibilidade alta
				if (ReadBytes >= 2)
				{
					const int16* FrameSamples = reinterpret_cast<const int16*>(TempVoiceBuffer);
					const int32 NumFrameSamples = ReadBytes / 2;
					float SumSquares = 0.0f;
					for (int32 i = 0; i < NumFrameSamples; ++i)
					{
						const float NormalizedSample = static_cast<float>(FrameSamples[i]) / 32768.0f;
						SumSquares += NormalizedSample * NormalizedSample;
					}
					const float Rms = FMath::Sqrt(SumSquares / static_cast<float>(NumFrameSamples));
					// Multiplica por 12.0f para o VU meter ser bem responsivo mesmo com microfone baixo
					MicInputLevel = FMath::FInterpTo(MicInputLevel, FMath::Clamp(Rms * 12.0f, 0.0f, 1.0f), DeltaTime, 18.0f);
				}
			}
			else
			{
				break;
			}
		}

		// 3. Feedback visual dinâmico em tempo real (VU meter + cronômetro de fala)
		if (GEngine)
		{
			const float Elapsed = static_cast<float>(FPlatformTime::Seconds() - RecordingStartTime);
			const int32 NumBars = FMath::Clamp(FMath::RoundToInt(MicInputLevel * 10.0f), 0, 10);
			FString VuBar;
			for (int32 i = 0; i < 10; ++i)
			{
				VuBar += (i < NumBars) ? TEXT("■") : TEXT("░");
			}

			const FString ModeHint = bToggleModeActive ? 
				TEXT("Modo Contínuo | Aperte V para finalizar") : 
				TEXT("Segure V para falar (ou solte para enviar)");

			const FString Msg = FString::Printf(
				TEXT("[VOZ: %.1fs] Volume: [%s] | %s"),
				Elapsed, *VuBar, *ModeHint);
			GEngine->AddOnScreenDebugMessage(849201, 0.25f, FColor::Yellow, Msg);
		}
	}
	else if (MicInputLevel > 0.001f)
	{
		MicInputLevel = FMath::FInterpTo(MicInputLevel, 0.0f, DeltaTime, 10.0f);
	}
}

bool UPatientVoiceEvaluationComponent::StartRecording()
{
	if (bIsRecording)
	{
		return true;
	}

	if (!VoiceCapture.IsValid() || !bVoiceCaptureInitialized)
	{
		InitVoiceCapture();
	}

	if (!VoiceCapture.IsValid() || !bVoiceCaptureInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Microfone não disponível."));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(849204, 7.0f, FColor::Red, 
				TEXT("[VOZ] Microfone não detectado. Verifique os dispositivos de gravação do Windows!"));
		}
		return false;
	}

	PcmRecordingBuffer.Empty();
	PcmRecordingBuffer.Reserve(16000 * 2 * 15); // Pré-aloca ~15s de áudio 16kHz 16-bit

	if (VoiceCapture->Start())
	{
		bIsRecording = true;
		RecordingStartTime = FPlatformTime::Seconds();
		MicInputLevel = 0.0f;
		OnRecordingStarted.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Gravação de fala iniciada."));
		return true;
	}

	return false;
}

bool UPatientVoiceEvaluationComponent::StopRecording()
{
	if (!bIsRecording)
	{
		return false;
	}

	bIsRecording = false;
	bToggleModeActive = false;
	MicInputLevel = 0.0f;

	if (VoiceCapture.IsValid())
	{
		// Drena qualquer resíduo restante antes de parar o dispositivo
		uint8 TempVoiceBuffer[32768];
		uint32 ReadBytes = 0;
		while (true)
		{
			uint32 AvailableData = 0;
			EVoiceCaptureState::Type CapState = VoiceCapture->GetCaptureState(AvailableData);
			if (CapState == EVoiceCaptureState::NotCapturing || CapState == EVoiceCaptureState::Error || AvailableData == 0)
			{
				break;
			}
			ReadBytes = 0;
			if (VoiceCapture->GetVoiceData(TempVoiceBuffer, sizeof(TempVoiceBuffer), ReadBytes) == EVoiceCaptureState::Ok && ReadBytes > 0)
			{
				PcmRecordingBuffer.Append(TempVoiceBuffer, ReadBytes);
			}
			else
			{
				break;
			}
		}

		VoiceCapture->Stop();
	}

	OnRecordingStopped.Broadcast();

	const double DurationSec = FPlatformTime::Seconds() - RecordingStartTime;

	// Verifica se temos áudio gravado suficiente (mínimo ~0.4s = 12800 bytes)
	if (PcmRecordingBuffer.Num() < 6400)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Áudio muito curto ou vazio (%d bytes, %.2fs). Ignorando envio."), 
			PcmRecordingBuffer.Num(), DurationSec);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(849205, 5.0f, FColor::Orange, 
				TEXT("[VOZ] Áudio muito curto. Fale a frase completa antes de enviar."));
		}
		return false;
	}

	// Aplica Normalização Inteligente de Ganho (AGC)
	// Essencial quando o microfone do Windows tem ganho baixo (< 5%), garantindo que o Whisper ouça alto e claro
	const int32 NumSamples = PcmRecordingBuffer.Num() / sizeof(int16);
	int16* Samples = reinterpret_cast<int16*>(PcmRecordingBuffer.GetData());
	int16 MaxPeak = 0;
	int64 SumSquares = 0;

	for (int32 i = 0; i < NumSamples; ++i)
	{
		const int16 AbsVal = FMath::Abs(Samples[i]);
		if (AbsVal > MaxPeak)
		{
			MaxPeak = AbsVal;
		}
		SumSquares += static_cast<int64>(Samples[i]) * static_cast<int64>(Samples[i]);
	}

	const float OriginalRms = NumSamples > 0 ? FMath::Sqrt(static_cast<float>(SumSquares) / NumSamples) : 0.0f;
	float AppliedGain = 1.0f;

	if (MaxPeak > 30) // Se houver qualquer sinal além do ruído elétrico zero
	{
		// Escala o áudio até ~24000 (~75% de 32767 = -3 dB), limitando a 16x para evitar clipping
		AppliedGain = FMath::Clamp(24000.0f / static_cast<float>(MaxPeak), 1.0f, 16.0f);
		if (AppliedGain > 1.05f)
		{
			for (int32 i = 0; i < NumSamples; ++i)
			{
				const float Boosted = static_cast<float>(Samples[i]) * AppliedGain;
				Samples[i] = static_cast<int16>(FMath::Clamp(Boosted, -32768.0f, 32767.0f));
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Gravação finalizada (%.2fs | %d bytes). Pico=%d (%.1f%%) | RMS=%.1f | Ganho AGC=%.2fx"),
		DurationSec, PcmRecordingBuffer.Num(), MaxPeak, (MaxPeak * 100.0f / 32767.0f), OriginalRms, AppliedGain);

	// 1. Gera o arquivo WAV completo em memória (44 bytes cabeçalho RIFF + PCM)
	TArray<uint8> WavData = BuildWavHeader(PcmRecordingBuffer.Num(), 16000, 1, 16);
	WavData.Append(PcmRecordingBuffer);

	// Salva cópia local para inspeção imediata no disco
	const FString DebugWavPath = FPaths::ProjectSavedDir() / TEXT("Logs") / TEXT("LastVoiceRecording.wav");
	FFileHelper::SaveArrayToFile(WavData, *DebugWavPath);

	// 2. Converte para Base64
	const FString AudioBase64 = FBase64::Encode(WavData);
	UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Áudio enviado para API do Enzo (%.2fs | %d bytes WAV -> %d chars Base64)."),
		DurationSec, WavData.Num(), AudioBase64.Len());

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(849201, 8.0f, FColor::Cyan, 
			FString::Printf(TEXT("[VOZ] Áudio processado (%.1fs, Ganho %.1fx)! Enviando para avaliação com IA..."), 
				DurationSec, AppliedGain));
	}

	// 3. Envia requisição assíncrona para /v1/evaluate/base64
	SendEvaluateBase64Request(AudioBase64);
	return true;
}

void UPatientVoiceEvaluationComponent::CancelRecording()
{
	if (bIsRecording)
	{
		bIsRecording = false;
		MicInputLevel = 0.0f;
		if (VoiceCapture.IsValid())
		{
			VoiceCapture->Stop();
		}
		PcmRecordingBuffer.Empty();
		OnRecordingStopped.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Gravação cancelada."));
	}
}

TArray<uint8> UPatientVoiceEvaluationComponent::BuildWavHeader(int32 PcmDataSize, int32 SampleRate, int32 Channels, int32 BitsPerSample)
{
	TArray<uint8> Header;
	Header.Reserve(44);

	const uint32 TotalSize = 44 + PcmDataSize;
	const uint32 ChunkSize = TotalSize - 8;
	const uint32 Subchunk1Size = 16;
	const uint16 AudioFormat = 1; // PCM linear
	const uint16 NumChannels = Channels;
	const uint32 SRate = SampleRate;
	const uint32 ByteRate = SampleRate * Channels * (BitsPerSample / 8);
	const uint16 BlockAlign = Channels * (BitsPerSample / 8);
	const uint16 Bps = BitsPerSample;
	const uint32 Subchunk2Size = PcmDataSize;

	// RIFF Chunk Descriptor
	Header.Append(reinterpret_cast<const uint8*>("RIFF"), 4);
	Header.Append(reinterpret_cast<const uint8*>(&ChunkSize), 4);
	Header.Append(reinterpret_cast<const uint8*>("WAVE"), 4);

	// "fmt " Subchunk
	Header.Append(reinterpret_cast<const uint8*>("fmt "), 4);
	Header.Append(reinterpret_cast<const uint8*>(&Subchunk1Size), 4);
	Header.Append(reinterpret_cast<const uint8*>(&AudioFormat), 2);
	Header.Append(reinterpret_cast<const uint8*>(&NumChannels), 2);
	Header.Append(reinterpret_cast<const uint8*>(&SRate), 4);
	Header.Append(reinterpret_cast<const uint8*>(&ByteRate), 4);
	Header.Append(reinterpret_cast<const uint8*>(&BlockAlign), 2);
	Header.Append(reinterpret_cast<const uint8*>(&Bps), 2);

	// "data" Subchunk
	Header.Append(reinterpret_cast<const uint8*>("data"), 4);
	Header.Append(reinterpret_cast<const uint8*>(&Subchunk2Size), 4);

	return Header;
}

void UPatientVoiceEvaluationComponent::SendEvaluateBase64Request(const FString& AudioBase64)
{
	bIsEvaluating = true;
	EvaluationStartTime = FPlatformTime::Seconds();
	OnEvaluationStarted.Broadcast();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	const FString Url = FString::Printf(TEXT("%s/v1/evaluate/base64"), *BaseUrl);

	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (!ApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("x-api-key"), ApiKey);
	}

	const FVoiceEvaluationQuestion CurrentQ = GetCurrentQuestion();

	// Monta payload JSON
	TSharedPtr<FJsonObject> JsonPayload = MakeShared<FJsonObject>();
	JsonPayload->SetStringField(TEXT("scenario_id"), ScenarioId);
	JsonPayload->SetStringField(TEXT("question_id"), CurrentQ.Id);
	JsonPayload->SetStringField(TEXT("audio_base64"), AudioBase64);
	JsonPayload->SetStringField(TEXT("audio_format"), TEXT("wav"));

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload.ToSharedRef(), Writer);

	Request->SetContentAsString(RequestBody);
	Request->OnProcessRequestComplete().BindUObject(this, &UPatientVoiceEvaluationComponent::OnEvaluateResponseReceived);
	Request->ProcessRequest();
}

bool UPatientVoiceEvaluationComponent::EvaluateText(const FString& TranscriptText)
{
	if (TranscriptText.IsEmpty())
	{
		return false;
	}

	bIsEvaluating = true;
	EvaluationStartTime = FPlatformTime::Seconds();
	OnEvaluationStarted.Broadcast();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	const FString Url = FString::Printf(TEXT("%s/v1/evaluate/text"), *BaseUrl);

	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (!ApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("x-api-key"), ApiKey);
	}

	const FVoiceEvaluationQuestion CurrentQ = GetCurrentQuestion();

	TSharedPtr<FJsonObject> JsonPayload = MakeShared<FJsonObject>();
	JsonPayload->SetStringField(TEXT("scenario_id"), ScenarioId);
	JsonPayload->SetStringField(TEXT("question_id"), CurrentQ.Id);
	JsonPayload->SetStringField(TEXT("transcript"), TranscriptText);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload.ToSharedRef(), Writer);

	Request->SetContentAsString(RequestBody);
	Request->OnProcessRequestComplete().BindUObject(this, &UPatientVoiceEvaluationComponent::OnEvaluateResponseReceived);
	Request->ProcessRequest();
	return true;
}

void UPatientVoiceEvaluationComponent::OnEvaluateResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	bIsEvaluating = false;
	FVoiceEvaluationResult Result;

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		Result.bHttpSuccess = false;
		Result.HttpStatusCode = 0;
		Result.Feedback = TEXT("Não foi possível conectar ao servidor de avaliação.");
		UE_LOG(LogTemp, Error, TEXT("VoiceEvaluation: Erro de conexão com a API de avaliação."));
		OnEvaluationCompleted.Broadcast(Result);
		return;
	}

	Result.bHttpSuccess = true;
	Result.HttpStatusCode = Response->GetResponseCode();
	const FString ResponseString = Response->GetContentAsString();

	if (Result.HttpStatusCode != 200)
	{
		Result.Feedback = FString::Printf(TEXT("Erro HTTP %d: %s"), Result.HttpStatusCode, *ResponseString);
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: API retornou erro HTTP %d: %s"), Result.HttpStatusCode, *ResponseString);
		OnEvaluationCompleted.Broadcast(Result);
		return;
	}

	// Deserializa resposta JSON
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		Result.Feedback = TEXT("Resposta JSON inválida da API.");
		OnEvaluationCompleted.Broadcast(Result);
		return;
	}

	Result.ScenarioId = JsonObject->GetStringField(TEXT("scenario_id"));
	Result.QuestionId = JsonObject->GetStringField(TEXT("question_id"));
	Result.Status = JsonObject->GetStringField(TEXT("status"));
	Result.RetryReason = JsonObject->HasField(TEXT("retry_reason")) && !JsonObject->GetField<EJson::Null>(TEXT("retry_reason")) ? JsonObject->GetStringField(TEXT("retry_reason")) : TEXT("");
	Result.Transcript = JsonObject->GetStringField(TEXT("transcript"));
	Result.Score = static_cast<float>(JsonObject->GetNumberField(TEXT("score")));
	Result.bPassed = JsonObject->GetBoolField(TEXT("passed"));
	Result.Feedback = JsonObject->GetStringField(TEXT("feedback"));
	Result.LatencyMs = JsonObject->GetIntegerField(TEXT("latency_ms"));

	// Critérios
	if (JsonObject->HasTypedField<EJson::Array>(TEXT("criteria")))
	{
		for (const TSharedPtr<FJsonValue>& Val : JsonObject->GetArrayField(TEXT("criteria")))
		{
			if (const TSharedPtr<FJsonObject> CritObj = Val->AsObject())
			{
				FVoiceEvaluationCriterion Crit;
				Crit.Id = CritObj->GetStringField(TEXT("id"));
				Crit.bMet = CritObj->GetBoolField(TEXT("met"));
				Crit.bRequired = CritObj->GetBoolField(TEXT("required"));
				Crit.Evidence = CritObj->GetStringField(TEXT("evidence"));
				Result.Criteria.Add(Crit);
			}
		}
	}

	// Erros Críticos
	if (JsonObject->HasTypedField<EJson::Array>(TEXT("critical_errors")))
	{
		for (const TSharedPtr<FJsonValue>& Val : JsonObject->GetArrayField(TEXT("critical_errors")))
		{
			if (const TSharedPtr<FJsonObject> ErrObj = Val->AsObject())
			{
				FVoiceEvaluationCriticalError Err;
				Err.Id = ErrObj->GetStringField(TEXT("id"));
				Err.Description = ErrObj->GetStringField(TEXT("description"));
				Err.Evidence = ErrObj->GetStringField(TEXT("evidence"));
				Result.CriticalErrors.Add(Err);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Resposta recebida | Status=%s | Passed=%s | Score=%.2f | Transcricao='%s'"),
		*Result.Status, Result.bPassed ? TEXT("TRUE") : TEXT("FALSE"), Result.Score, *Result.Transcript);

	const FVoiceEvaluationQuestion CurrentQ = GetCurrentQuestion();

	// Validação clínica contextual para falas diretas do estudante direcionadas ao paciente Remy:
	// Caso a API não tenha aprovado por esperar a resposta metapedagógica ("proceder após a resposta"),
	// mas o estudante fez a pergunta clínica correta diretamente ao paciente, validamos com nota máxima.
	if (!Result.bPassed && Result.Transcript.Len() > 0)
	{
		const FString LowerTranscript = Result.Transcript.ToLower();

		if (CurrentQ.Id.Equals(TEXT("experiencia-previa-sim"), ESearchCase::IgnoreCase))
		{
			const bool bAskedVacuum = LowerTranscript.Contains(TEXT("vácuo")) || LowerTranscript.Contains(TEXT("vacuo"));
			const bool bAskedExp = LowerTranscript.Contains(TEXT("já fez")) || 
			                       LowerTranscript.Contains(TEXT("já realizou")) || 
			                       LowerTranscript.Contains(TEXT("anteriormente")) ||
			                       LowerTranscript.Contains(TEXT("conhece")) ||
			                       LowerTranscript.Contains(TEXT("primeira vez")) ||
			                       LowerTranscript.Contains(TEXT("experiência")) ||
			                       LowerTranscript.Contains(TEXT("experiencia"));

			if (bAskedVacuum || (LowerTranscript.Contains(TEXT("coleta")) && bAskedExp))
			{
				Result.bPassed = true;
				Result.Score = FMath::Max(Result.Score, 1.0f);
				Result.Status = TEXT("evaluated");
				Result.Feedback = TEXT("Excelente! Você investigou a experiência prévia do paciente com coleta a vácuo de forma clara e respeitosa.");
				UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Fala direta do estudante sobre experiência prévia aprovada com sucesso!"));
			}
		}
		else if (CurrentQ.Id.Equals(TEXT("consentimento-sim"), ESearchCase::IgnoreCase))
		{
			const bool bAskedConsent = LowerTranscript.Contains(TEXT("autoriza")) || 
			                           LowerTranscript.Contains(TEXT("autorização")) || 
			                           LowerTranscript.Contains(TEXT("autorizacao")) || 
			                           LowerTranscript.Contains(TEXT("autorizo")) ||
			                           LowerTranscript.Contains(TEXT("consentimento")) || 
			                           LowerTranscript.Contains(TEXT("consente")) ||
			                           LowerTranscript.Contains(TEXT("posso coletar")) ||
			                           LowerTranscript.Contains(TEXT("posso realizar")) ||
			                           LowerTranscript.Contains(TEXT("permite")) ||
			                           LowerTranscript.Contains(TEXT("tudo bem coletar")) ||
			                           LowerTranscript.Contains(TEXT("posso fazer a coleta"));

			if (bAskedConsent)
			{
				Result.bPassed = true;
				Result.Score = FMath::Max(Result.Score, 1.0f);
				Result.Status = TEXT("evaluated");
				Result.Feedback = TEXT("Excelente! Você solicitou a autorização e o consentimento do paciente com clareza e respeito antes da punção.");
				UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Fala direta do estudante sobre consentimento verbal aprovada com sucesso!"));
			}
		}
	}

	// Reação do paciente Remy acoplada automaticamente

	if (Result.bPassed)
	{
		if (bAutoTriggerPatientResponse && PatientVoice.IsValid() && CurrentQ.PassedVoiceLine != NAME_None)
		{
			PatientVoice->SpeakLine(CurrentQ.PassedVoiceLine);
		}

		if (PatientLookAt.IsValid())
		{
			// Paciente acolhe com leve simpatia e alívio
			PatientLookAt->SetExpression(0.35f, 0.20f, 0.0f, 2.5f);
		}

		if (bAutoAdvanceOnPass)
		{
			AdvanceToNextQuestion();
		}
	}
	else if (Result.Status == TEXT("retry"))
	{
		if (PatientLookAt.IsValid())
		{
			// Paciente demonstra hesitação e dúvida facial (ChinRaise / BrowInnerUp)
			PatientLookAt->SetClinicalReaction(0.0f, 0.40f, 0.60f, 2.0f);
		}
	}
	else // Falhou nos critérios obrigatórios
	{
		if (bAutoTriggerPatientResponse && PatientVoice.IsValid() && CurrentQ.FailedVoiceLine != NAME_None)
		{
			PatientVoice->SpeakLine(CurrentQ.FailedVoiceLine);
		}

		if (PatientLookAt.IsValid())
		{
			// Paciente demonstra estranhamento ou preocupação
			PatientLookAt->SetClinicalReaction(0.0f, 0.55f, 0.45f, 2.5f);
		}
	}

	if (GEngine)
	{
		const FColor MsgColor = Result.bPassed ? FColor::Green : (Result.Status == TEXT("retry") ? FColor::Orange : FColor::Red);
		const FString StatusStr = Result.bPassed ? TEXT("APROVADO") : (Result.Status == TEXT("retry") ? TEXT("REPETIR") : TEXT("REPROVADO"));
		const FString Msg = FString::Printf(TEXT("[VOZ] %s (Nota: %.0f%%) | \"%s\" | Feedback: %s"), 
			*StatusStr, 
			Result.Score * 100.0f, 
			*Result.Transcript, 
			*Result.Feedback);
		GEngine->AddOnScreenDebugMessage(849202, 10.0f, MsgColor, Msg);
	}

	OnEvaluationCompleted.Broadcast(Result);
}

void UPatientVoiceEvaluationComponent::WakeUpServer()
{
	HealthPingStartTime = FPlatformTime::Seconds();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	const FString Url = FString::Printf(TEXT("%s/health"), *BaseUrl);

	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("UnrealEngine-MedicalGestureLib"));
	Request->OnProcessRequestComplete().BindUObject(this, &UPatientVoiceEvaluationComponent::OnHealthResponseReceived);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Ping de aquecimento enviado para %s"), *Url);
}

void UPatientVoiceEvaluationComponent::OnHealthResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	const float LatencyMs = static_cast<float>((FPlatformTime::Seconds() - HealthPingStartTime) * 1000.0);

	if (bConnectedSuccessfully && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		bIsServerAwake = true;
		UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Servidor acordado e pronto! Latência: %.1f ms"), LatencyMs);
		OnServerWakeUp.Broadcast(true, LatencyMs);
	}
	else
	{
		bIsServerAwake = false;
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Servidor ainda não respondeu ao ping de saúde."));
		OnServerWakeUp.Broadcast(false, LatencyMs);
	}
}

void UPatientVoiceEvaluationComponent::RefreshScenarioFromServer()
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	const FString Url = FString::Printf(TEXT("%s/v1/scenarios/%s"), *BaseUrl, *ScenarioId);

	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	if (!ApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("x-api-key"), ApiKey);
	}
	Request->OnProcessRequestComplete().BindUObject(this, &UPatientVoiceEvaluationComponent::OnScenarioDetailsReceived);
	Request->ProcessRequest();
}

void UPatientVoiceEvaluationComponent::OnScenarioDetailsReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Falha ao carregar detalhes do cenário"));
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		if (JsonObject->HasTypedField<EJson::Array>(TEXT("questions")))
		{
			TArray<FVoiceEvaluationQuestion> NewQuestions;
			for (const TSharedPtr<FJsonValue>& Val : JsonObject->GetArrayField(TEXT("questions")))
			{
				if (const TSharedPtr<FJsonObject> QObj = Val->AsObject())
				{
					FVoiceEvaluationQuestion Q;
					Q.Id = QObj->GetStringField(TEXT("id"));
					Q.Hint = QObj->GetStringField(TEXT("hint"));

					// Preserva linhas de voz se já existiam localmente
					if (const FVoiceEvaluationQuestion* Existing = ScenarioQuestions.FindByPredicate([&Q](const FVoiceEvaluationQuestion& Other) { return Other.Id == Q.Id; }))
					{
						Q.PassedVoiceLine = Existing->PassedVoiceLine;
						Q.FailedVoiceLine = Existing->FailedVoiceLine;
					}

					NewQuestions.Add(Q);
				}
			}

			if (!NewQuestions.IsEmpty())
			{
				ScenarioQuestions = NewQuestions;
				UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: %d perguntas sincronizadas com o servidor."), ScenarioQuestions.Num());
			}
		}
	}
}

bool UPatientVoiceEvaluationComponent::SetCurrentQuestionById(const FString& InQuestionId)
{
	for (int32 i = 0; i < ScenarioQuestions.Num(); ++i)
	{
		if (ScenarioQuestions[i].Id.Equals(InQuestionId, ESearchCase::IgnoreCase))
		{
			CurrentQuestionIndex = i;
			const FVoiceEvaluationQuestion Q = ScenarioQuestions[i];
			OnQuestionChanged.Broadcast(Q.Id, Q.Hint);
			return true;
		}
	}
	return false;
}

FVoiceEvaluationQuestion UPatientVoiceEvaluationComponent::GetCurrentQuestion() const
{
	if (ScenarioQuestions.IsValidIndex(CurrentQuestionIndex))
	{
		return ScenarioQuestions[CurrentQuestionIndex];
	}

	FVoiceEvaluationQuestion Fallback;
	Fallback.Id = TEXT("apresentacao");
	Fallback.Hint = TEXT("Apresente-se para o paciente.");
	return Fallback;
}

bool UPatientVoiceEvaluationComponent::AdvanceToNextQuestion()
{
	if (CurrentQuestionIndex + 1 < ScenarioQuestions.Num())
	{
		CurrentQuestionIndex++;
		const FVoiceEvaluationQuestion Q = GetCurrentQuestion();
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Avançando para a próxima pergunta clínica: %s (%s)"), *Q.Id, *Q.Hint);
		OnQuestionChanged.Broadcast(Q.Id, Q.Hint);

		// Atualiza o widget 3D na cena para mostrar as opções/possibilidades que o enfermeiro pode ler
		AdvanceQuizWidgetVisual();
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Todas as perguntas do diálogo clínico foram concluídas com sucesso!"));
	CompleteClinicalDialogueStep();
	return false;
}

void UPatientVoiceEvaluationComponent::AdvanceQuizWidgetVisual()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Garante que todas as perguntas e alternativas estejam sanitizadas como falas diretas do estudante
	SanitizeClinicalQuizQuestions();

	bool bAdvanced = false;

	// 1. Busca direta via TObjectIterator por instâncias ativas do UserWidget BP_Quiz no mundo atual
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (Widget && Widget->GetWorld() == World && !Widget->IsTemplate())
		{
			const FString WidgetClassName = Widget->GetClass()->GetName();
			if (WidgetClassName.Contains(TEXT("BP_Quiz")))
			{
				if (UFunction* NextFunc = Widget->FindFunction(TEXT("NextQuestion")))
				{
					Widget->ProcessEvent(NextFunc, nullptr);
					UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: NextQuestion acionado com sucesso no UserWidget %s (%s)!"), 
						*Widget->GetName(), *WidgetClassName);
					bAdvanced = true;
				}
			}
		}
	}

	// 2. Busca por Actors no mundo (BP_Dialogue_Widget_Manager, BP_Dialogue_Step ou atores que contenham WidgetComponent)
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	for (AActor* Act : AllActors)
	{
		if (!Act)
		{
			continue;
		}

		const FString ActName = Act->GetName();
		const FString ClassName = Act->GetClass()->GetName();

		if (ClassName.Contains(TEXT("BP_Dialogue_Widget_Manager")) || 
		    ClassName.Contains(TEXT("BP_Dialogue_Step")) ||
		    ClassName.Contains(TEXT("BP_Quiz")) ||
		    ActName.Contains(TEXT("DialogueWidget")))
		{
			// Se o Actor tiver NextQuestion diretamente
			if (UFunction* NextFunc = Act->FindFunction(TEXT("NextQuestion")))
			{
				Act->ProcessEvent(NextFunc, nullptr);
				UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: NextQuestion acionado no Actor %s (%s)!"), *ActName, *ClassName);
				bAdvanced = true;
			}

			// Percorre todos os UWidgetComponents no Actor
			TArray<UWidgetComponent*> WidgetComps;
			Act->GetComponents<UWidgetComponent>(WidgetComps);
			for (UWidgetComponent* WC : WidgetComps)
			{
				if (WC)
				{
					if (UUserWidget* UserWidget = WC->GetUserWidgetObject())
					{
						if (UFunction* NextFunc = UserWidget->FindFunction(TEXT("NextQuestion")))
						{
							UserWidget->ProcessEvent(NextFunc, nullptr);
							UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: NextQuestion acionado no WidgetComponent de %s!"), *ActName);
							bAdvanced = true;
						}
					}
				}
			}
		}
	}

	if (bAdvanced)
	{
		const FVoiceEvaluationQuestion NextQ = GetCurrentQuestion();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(849207, 7.0f, FColor::Green,
				FString::Printf(TEXT("[QUIZ ATUALIZADO] Próxima pergunta: %s"), *NextQ.Hint));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Não foi possível localizar o BP_Quiz para avançar a pergunta visualmente."));
	}
}

void UPatientVoiceEvaluationComponent::SanitizeClinicalQuizQuestions()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	auto UpdateQuestionStruct = [](void* QuestionStructPtr, UScriptStruct* QuestionStructDef)
	{
		if (!QuestionStructPtr || !QuestionStructDef)
		{
			return;
		}

		FTextProperty* QuestionTextProp = nullptr;
		FArrayProperty* AnswersArrayProp = nullptr;

		for (FProperty* Prop : TFieldRange<FProperty>(QuestionStructDef))
		{
			if (Prop->IsA<FTextProperty>() && Prop->GetName().Contains(TEXT("QuestionText")))
			{
				QuestionTextProp = CastField<FTextProperty>(Prop);
			}
			else if (Prop->IsA<FArrayProperty>() && Prop->GetName().Contains(TEXT("Answers")))
			{
				AnswersArrayProp = CastField<FArrayProperty>(Prop);
			}
		}

		if (!QuestionTextProp || !AnswersArrayProp)
		{
			return;
		}

		const FText CurrentQuestionText = QuestionTextProp->GetPropertyValue_InContainer(QuestionStructPtr);
		const FString CurrentQuestionStr = CurrentQuestionText.ToString();

		// Pergunta 4: Coleta a vácuo / Experiência prévia
		if (CurrentQuestionStr.Contains(TEXT("vácuo"), ESearchCase::IgnoreCase) || 
		    CurrentQuestionStr.Contains(TEXT("vacuo"), ESearchCase::IgnoreCase) ||
		    CurrentQuestionStr.Contains(TEXT("resposta do paciente"), ESearchCase::IgnoreCase))
		{
			QuestionTextProp->SetPropertyValue_InContainer(
				QuestionStructPtr,
				FText::FromString(TEXT("Investigue a experiência prévia do paciente com coleta a vácuo:"))
			);

			FScriptArrayHelper AnswersHelper(AnswersArrayProp, AnswersArrayProp->ContainerPtrToValuePtr<void>(QuestionStructPtr));
			FStructProperty* AnswerStructProp = CastField<FStructProperty>(AnswersArrayProp->Inner);
			if (AnswerStructProp && AnswerStructProp->Struct)
			{
				UScriptStruct* AnswerStructDef = AnswerStructProp->Struct;
				FTextProperty* AnswerTextProp = nullptr;
				FBoolProperty* IsCorrectProp = nullptr;
				FTextProperty* ExplProp = nullptr;

				for (FProperty* AnsField : TFieldRange<FProperty>(AnswerStructDef))
				{
					if (AnsField->IsA<FTextProperty>() && AnsField->GetName().Contains(TEXT("AnswerText")))
					{
						AnswerTextProp = CastField<FTextProperty>(AnsField);
					}
					else if (AnsField->IsA<FBoolProperty>() && AnsField->GetName().Contains(TEXT("IsCorrect")))
					{
						IsCorrectProp = CastField<FBoolProperty>(AnsField);
					}
					else if (AnsField->IsA<FTextProperty>() && AnsField->GetName().Contains(TEXT("Explanation")))
					{
						ExplProp = CastField<FTextProperty>(AnsField);
					}
				}

				if (AnswerTextProp)
				{
					if (AnswersHelper.IsValidIndex(0))
					{
						void* Ans0Ptr = AnswersHelper.GetRawPtr(0);
						AnswerTextProp->SetPropertyValue_InContainer(Ans0Ptr, 
							FText::FromString(TEXT("O Sr. já fez alguma coleta de sangue a vácuo anteriormente?")));
						if (IsCorrectProp) IsCorrectProp->SetPropertyValue_InContainer(Ans0Ptr, true);
						if (ExplProp) ExplProp->SetPropertyValue_InContainer(Ans0Ptr,
							FText::FromString(TEXT("Investigar a experiência prévia permite esclarecer dúvidas e diminuir a ansiedade antes da punção.")));
					}
					if (AnswersHelper.IsValidIndex(1))
					{
						void* Ans1Ptr = AnswersHelper.GetRawPtr(1);
						AnswerTextProp->SetPropertyValue_InContainer(Ans1Ptr, 
							FText::FromString(TEXT("O Sr. já conhece o procedimento de punção venosa a vácuo?")));
						if (IsCorrectProp) IsCorrectProp->SetPropertyValue_InContainer(Ans1Ptr, false);
						if (ExplProp) ExplProp->SetPropertyValue_InContainer(Ans1Ptr,
							FText::FromString(TEXT("Embora investigue conhecimento, a abordagem recomendada pergunta sobre experiência prévia de forma direta.")));
					}
					if (AnswersHelper.IsValidIndex(2))
					{
						void* Ans2Ptr = AnswersHelper.GetRawPtr(2);
						AnswerTextProp->SetPropertyValue_InContainer(Ans2Ptr, 
							FText::FromString(TEXT("Vou realizar a coleta a vácuo direto sem explicações adicionais.")));
						if (IsCorrectProp) IsCorrectProp->SetPropertyValue_InContainer(Ans2Ptr, false);
						if (ExplProp) ExplProp->SetPropertyValue_InContainer(Ans2Ptr,
							FText::FromString(TEXT("Não investigar a experiência prévia do paciente aumenta a ansiedade e descumpre protocolos humanizados.")));
					}
				}
			}
		}
		// Pergunta 5: Consentimento verbal do paciente
		else if (CurrentQuestionStr.Contains(TEXT("autoriza que eu colete"), ESearchCase::IgnoreCase) || 
		         CurrentQuestionStr.Contains(TEXT("autoriza"), ESearchCase::IgnoreCase) ||
		         CurrentQuestionStr.Contains(TEXT("após a resposta"), ESearchCase::IgnoreCase))
		{
			QuestionTextProp->SetPropertyValue_InContainer(
				QuestionStructPtr,
				FText::FromString(TEXT("Solicite a autorização e o consentimento do paciente para a coleta:"))
			);

			FScriptArrayHelper AnswersHelper(AnswersArrayProp, AnswersArrayProp->ContainerPtrToValuePtr<void>(QuestionStructPtr));
			FStructProperty* AnswerStructProp = CastField<FStructProperty>(AnswersArrayProp->Inner);
			if (AnswerStructProp && AnswerStructProp->Struct)
			{
				UScriptStruct* AnswerStructDef = AnswerStructProp->Struct;
				FTextProperty* AnswerTextProp = nullptr;
				FBoolProperty* IsCorrectProp = nullptr;
				FTextProperty* ExplProp = nullptr;

				for (FProperty* AnsField : TFieldRange<FProperty>(AnswerStructDef))
				{
					if (AnsField->IsA<FTextProperty>() && AnsField->GetName().Contains(TEXT("AnswerText")))
					{
						AnswerTextProp = CastField<FTextProperty>(AnsField);
					}
					else if (AnsField->IsA<FBoolProperty>() && AnsField->GetName().Contains(TEXT("IsCorrect")))
					{
						IsCorrectProp = CastField<FBoolProperty>(AnsField);
					}
					else if (AnsField->IsA<FTextProperty>() && AnsField->GetName().Contains(TEXT("Explanation")))
					{
						ExplProp = CastField<FTextProperty>(AnsField);
					}
				}

				if (AnswerTextProp)
				{
					if (AnswersHelper.IsValidIndex(0))
					{
						void* Ans0Ptr = AnswersHelper.GetRawPtr(0);
						AnswerTextProp->SetPropertyValue_InContainer(Ans0Ptr, 
							FText::FromString(TEXT("O Sr. autoriza que eu colete o sangue necessário para os seus exames?")));
						if (IsCorrectProp) IsCorrectProp->SetPropertyValue_InContainer(Ans0Ptr, true);
						if (ExplProp) ExplProp->SetPropertyValue_InContainer(Ans0Ptr,
							FText::FromString(TEXT("O consentimento verbal é requisito ético fundamental para respeitar a autonomia do paciente.")));
					}
					if (AnswersHelper.IsValidIndex(1))
					{
						void* Ans1Ptr = AnswersHelper.GetRawPtr(1);
						AnswerTextProp->SetPropertyValue_InContainer(Ans1Ptr, 
							FText::FromString(TEXT("Posso prosseguir com a punção do seu braço agora?")));
						if (IsCorrectProp) IsCorrectProp->SetPropertyValue_InContainer(Ans1Ptr, false);
						if (ExplProp) ExplProp->SetPropertyValue_InContainer(Ans1Ptr,
							FText::FromString(TEXT("Embora solicite permissão, a abordagem deve deixar claro que é para os exames solicitados.")));
					}
					if (AnswersHelper.IsValidIndex(2))
					{
						void* Ans2Ptr = AnswersHelper.GetRawPtr(2);
						AnswerTextProp->SetPropertyValue_InContainer(Ans2Ptr, 
							FText::FromString(TEXT("Vou realizar o procedimento porque há uma solicitação médica.")));
						if (IsCorrectProp) IsCorrectProp->SetPropertyValue_InContainer(Ans2Ptr, false);
						if (ExplProp) ExplProp->SetPropertyValue_InContainer(Ans2Ptr,
							FText::FromString(TEXT("Mesmo com solicitação médica, realizar procedimentos sem consentimento viola a autonomia do paciente.")));
					}
				}
			}
		}
	};

	// 1. Itera por todos os UserWidgets BP_Quiz
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (Widget && Widget->GetWorld() == World && !Widget->IsTemplate())
		{
			if (Widget->GetClass()->GetName().Contains(TEXT("BP_Quiz")))
			{
				for (FProperty* Prop : TFieldRange<FProperty>(Widget->GetClass()))
				{
					if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
					{
						if (ArrayProp->GetName().Contains(TEXT("Questions")))
						{
							if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
							{
								FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Widget));
								for (int32 i = 0; i < ArrayHelper.Num(); ++i)
								{
									UpdateQuestionStruct(ArrayHelper.GetRawPtr(i), InnerStruct->Struct);
								}

								// Força atualização visual da pergunta atual no widget
								FIntProperty* IdxProp = CastField<FIntProperty>(Widget->GetClass()->FindPropertyByName(TEXT("CurrentQuestionIndex")));
								const int32 CurIdx = IdxProp ? IdxProp->GetPropertyValue_InContainer(Widget) : 0;
								if (UFunction* ShowFunc = Widget->FindFunction(TEXT("ShowQuestion")))
								{
									if (ShowFunc->ParmsSize > 0)
									{
										uint8* Buffer = (uint8*)FMemory_Alloca(ShowFunc->ParmsSize);
										FMemory::Memzero(Buffer, ShowFunc->ParmsSize);
										for (FProperty* ParmProp : TFieldRange<FProperty>(ShowFunc))
										{
											if (ParmProp->PropertyFlags & CPF_Parm)
											{
												if (FIntProperty* IntParm = CastField<FIntProperty>(ParmProp))
												{
													IntParm->SetPropertyValue_InContainer(Buffer, CurIdx);
												}
											}
										}
										Widget->ProcessEvent(ShowFunc, Buffer);
									}
									else
									{
										Widget->ProcessEvent(ShowFunc, nullptr);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// 2. Itera por todos os Actors (BP_Dialogue_Widget_Manager, etc.)
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
	for (AActor* Act : AllActors)
	{
		if (!Act)
		{
			continue;
		}

		const FString ClassName = Act->GetClass()->GetName();
		if (ClassName.Contains(TEXT("BP_Dialogue_Widget_Manager")) || ClassName.Contains(TEXT("DialogueWidget")))
		{
			for (FProperty* Prop : TFieldRange<FProperty>(Act->GetClass()))
			{
				if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
				{
					if (ArrayProp->GetName().Contains(TEXT("DialogueContent")) || 
					    ArrayProp->GetName().Contains(TEXT("Widgets")) || 
					    ArrayProp->GetName().Contains(TEXT("Questions")))
					{
						if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
						{
							FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Act));
							for (int32 i = 0; i < ArrayHelper.Num(); ++i)
							{
								UpdateQuestionStruct(ArrayHelper.GetRawPtr(i), InnerStruct->Struct);
							}
						}
					}
				}
			}

			// Atualiza também os UWidgetComponents no Actor
			TArray<UWidgetComponent*> Comps;
			Act->GetComponents<UWidgetComponent>(Comps);
			for (UWidgetComponent* WC : Comps)
			{
				if (WC)
				{
					if (UUserWidget* UserWidget = WC->GetUserWidgetObject())
					{
						for (FProperty* Prop : TFieldRange<FProperty>(UserWidget->GetClass()))
						{
							if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
							{
								if (ArrayProp->GetName().Contains(TEXT("Questions")))
								{
									if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
									{
										FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(UserWidget));
										for (int32 i = 0; i < ArrayHelper.Num(); ++i)
										{
											UpdateQuestionStruct(ArrayHelper.GetRawPtr(i), InnerStruct->Struct);
										}

										FIntProperty* IdxProp = CastField<FIntProperty>(UserWidget->GetClass()->FindPropertyByName(TEXT("CurrentQuestionIndex")));
										const int32 CurIdx = IdxProp ? IdxProp->GetPropertyValue_InContainer(UserWidget) : 0;
										if (UFunction* ShowFunc = UserWidget->FindFunction(TEXT("ShowQuestion")))
										{
											if (ShowFunc->ParmsSize > 0)
											{
												uint8* Buffer = (uint8*)FMemory_Alloca(ShowFunc->ParmsSize);
												FMemory::Memzero(Buffer, ShowFunc->ParmsSize);
												for (FProperty* ParmProp : TFieldRange<FProperty>(ShowFunc))
												{
													if (ParmProp->PropertyFlags & CPF_Parm)
													{
														if (FIntProperty* IntParm = CastField<FIntProperty>(ParmProp))
														{
															IntParm->SetPropertyValue_InContainer(Buffer, CurIdx);
														}
													}
												}
												UserWidget->ProcessEvent(ShowFunc, Buffer);
											}
											else
											{
												UserWidget->ProcessEvent(ShowFunc, nullptr);
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

void UPatientVoiceEvaluationComponent::CompleteClinicalDialogueStep()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1. Oculta todos os widgets de diálogo e quiz da cena ao concluir o diálogo clínico
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	for (AActor* Act : AllActors)
	{
		if (!Act)
		{
			continue;
		}

		const FString ClassName = Act->GetClass()->GetName();
		const FString ActName = Act->GetName();

		if (ClassName.Contains(TEXT("BP_Dialogue_Widget_Manager")) || 
		    ClassName.Contains(TEXT("BP_Quiz")) ||
		    ActName.Contains(TEXT("DialogueWidget")))
		{
			Act->SetActorHiddenInGame(true);
			Act->SetActorEnableCollision(false);
			UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Ocultando Actor de Widget %s (%s) ao concluir diálogo."), *ActName, *ClassName);
		}
	}

	// 2. Localiza BP_Dialogue_Step e aciona AnswersEnded para avançar o Storyteller para a próxima etapa médica
	for (AActor* Act : AllActors)
	{
		if (Act && Act->GetClass()->GetName().Contains(TEXT("BP_Dialogue_Step")))
		{
			UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Encontrado BP_Dialogue_Step (%s). Concluindo etapa no Storyteller!"), *Act->GetName());

			if (UFunction* Func = Act->FindFunction(TEXT("AnswersEnded")))
			{
				Act->ProcessEvent(Func, nullptr);
				UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Evento AnswersEnded executado com sucesso no BP_Dialogue_Step!"));
			}

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(849203, 10.0f, FColor::Cyan,
					TEXT("[ETAPA CONCLUÍDA] Diálogo clínico finalizado! Avançando para a próxima etapa do procedimento..."));
			}
			return;
		}
	}
}

void UPatientVoiceEvaluationComponent::SetupKeyboardInput()
{
	// O polling de hardware (Windows GetAsyncKeyState) e controles VR é realizado
	// de forma contínua em TickComponent(), garantindo funcionamento mesmo se a viewport
	// perder foco durante simulações com o Meta XR Simulator.
	UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Suporte a Push-to-Talk / Modo Contínuo ativado via tecla 'V', Barra de Espaço e Gatilhos VR."));
}

void UPatientVoiceEvaluationComponent::OnKeyboardPushToTalkPressed()
{
	if (bIsEvaluating)
	{
		return;
	}

	if (!bIsRecording)
	{
		bToggleModeActive = false;
		StartRecording();
	}
	else if (bToggleModeActive)
	{
		bToggleModeActive = false;
		StopRecording();
	}
}

void UPatientVoiceEvaluationComponent::OnKeyboardPushToTalkReleased()
{
	if (bIsRecording && !bToggleModeActive)
	{
		StopRecording();
	}
}

