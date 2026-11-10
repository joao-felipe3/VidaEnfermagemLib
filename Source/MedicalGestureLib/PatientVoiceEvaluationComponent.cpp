// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientVoiceEvaluationComponent.h"

#include "Dom/JsonObject.h"
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
#include "VoiceModule.h"

UPatientVoiceEvaluationComponent::UPatientVoiceEvaluationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPatientVoiceEvaluationComponent::BeginPlay()
{
	Super::BeginPlay();

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
	if (FVoiceModule::IsAvailable())
	{
		VoiceCapture = FVoiceModule::Get().CreateVoiceCapture(TEXT(""));
		if (VoiceCapture.IsValid())
		{
			// 16 kHz, 1 canal (16-bit mono PCM padronizado para STT Whisper)
			bVoiceCaptureInitialized = VoiceCapture->Init(TEXT(""), 16000, 1);
			if (bVoiceCaptureInitialized)
			{
				UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Captura de voz inicializada com sucesso (16kHz mono)."));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Falha ao inicializar dispositivo de captura de áudio."));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Módulo Voice não disponível nesta plataforma."));
	}
}

void UPatientVoiceEvaluationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsRecording && VoiceCapture.IsValid())
	{
		uint32 AvailableData = 0;
		EVoiceCaptureState::Type State = VoiceCapture->GetCaptureState(AvailableData);

		if (State == EVoiceCaptureState::Ok && AvailableData > 0)
		{
			const int32 PrevSize = PcmRecordingBuffer.Num();
			PcmRecordingBuffer.AddUninitialized(AvailableData);
			uint32 ReadBytes = 0;
			VoiceCapture->GetVoiceData(PcmRecordingBuffer.GetData() + PrevSize, AvailableData, ReadBytes);

			// Computa amplitude média para medidor visual (RMS de inteiros 16-bit)
			if (ReadBytes >= 2)
			{
				const int16* Samples = reinterpret_cast<const int16*>(PcmRecordingBuffer.GetData() + PrevSize);
				const int32 NumSamples = ReadBytes / 2;
				float SumSquares = 0.0f;
				for (int32 i = 0; i < NumSamples; ++i)
				{
					const float NormalizedSample = static_cast<float>(Samples[i]) / 32768.0f;
					SumSquares += NormalizedSample * NormalizedSample;
				}
				const float Rms = FMath::Sqrt(SumSquares / static_cast<float>(NumSamples));
				MicInputLevel = FMath::FInterpTo(MicInputLevel, FMath::Clamp(Rms * 4.5f, 0.0f, 1.0f), DeltaTime, 14.0f);
			}
		}
		else if (State == EVoiceCaptureState::NoData)
		{
			MicInputLevel = FMath::FInterpTo(MicInputLevel, 0.0f, DeltaTime, 8.0f);
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
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Microfone não disponível para gravação."));
		return false;
	}

	PcmRecordingBuffer.Empty();
	PcmRecordingBuffer.Reserve(16000 * 2 * 10); // Pré-aloca ~10s de áudio 16kHz 16-bit

	if (VoiceCapture->Start())
	{
		bIsRecording = true;
		MicInputLevel = 0.0f;
		OnRecordingStarted.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Gravação de fala iniciada (Push-to-Talk)."));
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
	MicInputLevel = 0.0f;

	if (VoiceCapture.IsValid())
	{
		VoiceCapture->Stop();

		// Esvazia os últimos bytes disponíveis
		uint32 AvailableData = 0;
		if (VoiceCapture->GetCaptureState(AvailableData) == EVoiceCaptureState::Ok && AvailableData > 0)
		{
			const int32 PrevSize = PcmRecordingBuffer.Num();
			PcmRecordingBuffer.AddUninitialized(AvailableData);
			uint32 ReadBytes = 0;
			VoiceCapture->GetVoiceData(PcmRecordingBuffer.GetData() + PrevSize, AvailableData, ReadBytes);
		}
	}

	OnRecordingStopped.Broadcast();

	// Verifica se temos áudio gravado suficiente (mínimo ~0.3s = 9600 bytes)
	if (PcmRecordingBuffer.Num() < 4800)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceEvaluation: Áudio muito curto ou vazio (%d bytes). Ignorando envio."), PcmRecordingBuffer.Num());
		return false;
	}

	// 1. Gera o arquivo WAV completo em memória (44 bytes cabeçalho RIFF + PCM)
	TArray<uint8> WavData = BuildWavHeader(PcmRecordingBuffer.Num(), 16000, 1, 16);
	WavData.Append(PcmRecordingBuffer);

	// 2. Converte para Base64
	const FString AudioBase64 = FBase64::Encode(WavData);
	UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Áudio empacotado em WAV Base64 (%d bytes WAV -> %d chars Base64). Enviando para API..."),
		WavData.Num(), AudioBase64.Len());

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

	// Reação do paciente Remy acoplada automaticamente
	const FVoiceEvaluationQuestion CurrentQ = GetCurrentQuestion();

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
		UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Avançando para a próxima pergunta: %s"), *Q.Id);
		OnQuestionChanged.Broadcast(Q.Id, Q.Hint);
		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("VoiceEvaluation: Todas as perguntas do diálogo clínico foram concluídas!"));
	return false;
}
