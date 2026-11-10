// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "PatientVoiceEvaluationComponent.generated.h"

class UPatientVoiceComponent;
class UPatientLookAtComponent;
class IVoiceCapture;

USTRUCT(BlueprintType)
struct FVoiceEvaluationCriterion
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	bool bMet = false;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	bool bRequired = false;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Evidence;
};

USTRUCT(BlueprintType)
struct FVoiceEvaluationCriticalError
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Evidence;
};

USTRUCT(BlueprintType)
struct FVoiceEvaluationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	bool bHttpSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	int32 HttpStatusCode = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString ScenarioId;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString QuestionId;

	// "evaluated" ou "retry"
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Status;

	// "empty_transcript", "unintelligible" ou vazio
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString RetryReason;

	// O que o aluno falou (transcrito pelo STT)
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Transcript;

	// Nota atribuída (0.0 a 1.0)
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	float Score = 0.0f;

	// Se a resposta atendeu aos critérios mínimos obrigatórios
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	bool bPassed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	TArray<FVoiceEvaluationCriterion> Criteria;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	TArray<FVoiceEvaluationCriticalError> CriticalErrors;

	// Feedback pedagógico gerado para o estudante
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	FString Feedback;

	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation")
	int32 LatencyMs = 0;
};

USTRUCT(BlueprintType)
struct FVoiceEvaluationQuestion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation")
	FString Hint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation")
	FName PassedVoiceLine = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation")
	FName FailedVoiceLine = NAME_None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVoiceRecordingStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVoiceRecordingStopped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVoiceEvaluationStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoiceEvaluationCompleted, const FVoiceEvaluationResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVoiceServerWakeUp, bool, bIsAwake, float, LatencyMs);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVoiceQuestionChanged, const FString&, QuestionId, const FString&, Hint);

/**
 * Componente que integra a API de Avaliação por Voz ao Projeto VIDA Enfermagem.
 * Captura a fala do estudante via microfone (Push-to-Talk), converte para WAV base64,
 * envia para a API de avaliação (Whisper + LLM), e acopla a resposta ao paciente Remy
 * (voz, lip sync e reações faciais clínicas).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MEDICALGESTURELIB_API UPatientVoiceEvaluationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPatientVoiceEvaluationComponent();

	// URL base do serviço na nuvem (ex.: https://voice-eval-api.onrender.com)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation|API")
	FString BaseUrl = TEXT("https://voice-eval-api.onrender.com");

	// Chave de autenticação da API (x-api-key)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation|API")
	FString ApiKey = TEXT("wcP90Sipv_00QbUM8O_NBDsALc76AJSX6bt1A2aXLtg");

	// ID do cenário clínico avaliado
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation|API")
	FString ScenarioId = TEXT("coleta-sangue");

	// Se verdadeiro, dispara um ping /health no BeginPlay para acordar o container do Render
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation|API")
	bool bAutoWakeUpServerOnStart = true;

	// Se verdadeiro, avança automaticamente para a próxima pergunta quando o aluno passa
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation|Flow")
	bool bAutoAdvanceOnPass = true;

	// Se verdadeiro, faz o Remy responder com voz e lip sync quando o aluno passa
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation|Flow")
	bool bAutoTriggerPatientResponse = true;

	// Perguntas configuradas para o cenário clínico
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Evaluation|Flow")
	TArray<FVoiceEvaluationQuestion> ScenarioQuestions;

	// Índice da pergunta atual
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation|Flow")
	int32 CurrentQuestionIndex = 0;

	// Se o microfone está gravando neste momento
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation|State")
	bool bIsRecording = false;

	// Se uma requisição de avaliação está em andamento
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation|State")
	bool bIsEvaluating = false;

	// Se o servidor backend já confirmou estar acordado
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation|State")
	bool bIsServerAwake = false;

	// Nível atual de volume do microfone (0.0 a 1.0) para medidores em VR
	UPROPERTY(BlueprintReadOnly, Category = "Voice Evaluation|State")
	float MicInputLevel = 0.0f;

	// Eventos Dispatchers
	UPROPERTY(BlueprintAssignable, Category = "Voice Evaluation|Events")
	FOnVoiceRecordingStarted OnRecordingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Voice Evaluation|Events")
	FOnVoiceRecordingStopped OnRecordingStopped;

	UPROPERTY(BlueprintAssignable, Category = "Voice Evaluation|Events")
	FOnVoiceEvaluationStarted OnEvaluationStarted;

	UPROPERTY(BlueprintAssignable, Category = "Voice Evaluation|Events")
	FOnVoiceEvaluationCompleted OnEvaluationCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Voice Evaluation|Events")
	FOnVoiceServerWakeUp OnServerWakeUp;

	UPROPERTY(BlueprintAssignable, Category = "Voice Evaluation|Events")
	FOnVoiceQuestionChanged OnQuestionChanged;

	// --- FUNÇÕES BLUEPRINT ---

	// Inicia a gravação de áudio do microfone (ex: quando o aluno pressiona Push-to-Talk)
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	bool StartRecording();

	// Para a gravação, empacota o áudio WAV em base64 e envia para a API avaliar
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	bool StopRecording();

	// Cancela a gravação atual sem enviar nada para a API
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	void CancelRecording();

	// Envia um texto diretamente para a API (útil para testes em Desktop ou sem microfone)
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	bool EvaluateText(const FString& TranscriptText);

	// Envia um ping para o endpoint /health para aquecer o serviço no Render
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	void WakeUpServer();

	// Busca a lista de perguntas e critérios do cenário no servidor
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	void RefreshScenarioFromServer();

	// Define a pergunta atual pelo ID
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	bool SetCurrentQuestionById(const FString& InQuestionId);

	// Retorna a pergunta atual
	UFUNCTION(BlueprintPure, Category = "Voice Evaluation")
	FVoiceEvaluationQuestion GetCurrentQuestion() const;

	// Avança manualmente para a próxima pergunta
	UFUNCTION(BlueprintCallable, Category = "Voice Evaluation")
	bool AdvanceToNextQuestion();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void InitVoiceCapture();
	void ProcessVoiceBuffer(const TArray<uint8>& PcmData);
	static TArray<uint8> BuildWavHeader(int32 PcmDataSize, int32 SampleRate = 16000, int32 Channels = 1, int32 BitsPerSample = 16);
	void SendEvaluateBase64Request(const FString& AudioBase64);
	void OnEvaluateResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnHealthResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnScenarioDetailsReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	TSharedPtr<IVoiceCapture> VoiceCapture;
	TArray<uint8> PcmRecordingBuffer;
	bool bVoiceCaptureInitialized = false;

	TWeakObjectPtr<UPatientVoiceComponent> PatientVoice;
	TWeakObjectPtr<UPatientLookAtComponent> PatientLookAt;

	double HealthPingStartTime = 0.0;
	double EvaluationStartTime = 0.0;
};
