// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PatientVoiceComponent.generated.h"

class UAudioComponent;
class USkeletalMeshComponent;
class UWidget;
class USoundAttenuation;
class USoundBase;
class USoundWave;

// Linha do DataTable gerado por Tools/VoiceGen (DT_FalasPaciente.csv)
USTRUCT(BlueprintType)
struct FPatientVoiceLine : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	int32 Passo = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FString Gatilho;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FText Texto;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TSoftObjectPtr<USoundBase> Som;

	// Formas de boca do Rhubarb Lip Sync no formato "inicio:forma;..." (ex.: "0.00:X;0.06:D")
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FString Visemas;

	// Texto da pergunta ou da resposta do quiz que dispara esta fala (igual ao da cena)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FString Disparo;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPatientSpeechStarted, FName, LineId, const FText&, Texto);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPatientSpeechFinished, FName, LineId);

// Toca as falas do paciente a partir da cabeça (áudio espacial) e anima a boca: pelos visemas da
// linha quando existirem, senão pelo envelope do áudio (só JawOpen).
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class MEDICALGESTURELIB_API UPatientVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPatientVoiceComponent();

	// DataTable com linhas FPatientVoiceLine
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
	TObjectPtr<UDataTable> VoiceLines;

	// Carrega todos os sons no BeginPlay para evitar travadas no headset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
	bool bPreloadLines = true;

	// Vazio = atenuação espacial padrão (voz audível a até FalloffDistance cm da cabeça)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Spatial")
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Spatial", meta = (ClampMin = "0"))
	float FalloffDistance = 1200.0f;

	// Osso/socket do paciente de onde a voz sai
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Spatial")
	FName HeadBoneName = TEXT("Head");

	// Morph targets criados por Tools/LipSync/criar_visemas_remy.py; deixe vazio para ignorar
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName JawOpenMorph = TEXT("JawOpen");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName MouthPuckerMorph = TEXT("MouthPucker");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName MouthWideMorph = TEXT("MouthWide");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName LowerLipInMorph = TEXT("LowerLipIn");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName LipsPressMorph = TEXT("LipsPress");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName UpperLipUpMorph = TEXT("UpperLipUp");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName TongueUpMorph = TEXT("TongueUp");

	// Intensidade geral da boca (1 = formas completas)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0", ClampMax = "1.5"))
	float MouthIntensity = 1.0f;

	// Atraso dos visemas em segundos, para compensar a latência de áudio do dispositivo
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	float VisemeTimeOffset = 0.0f;

	// Multiplicador do envelope do áudio (0..1 de volume). Sem visemas, é a abertura da boca;
	// com visemas, modula a mandíbula pelo volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0"))
	float EnvelopeGain = 4.0f;

	// Quanto o volume da voz abre ou fecha a mandíbula em relação ao visema (0 = nada)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0", ClampMax = "1"))
	float EnvelopeInfluence = 0.35f;

	// Antecipação do próximo visema em segundos (coarticulação: a boca já se prepara para o som seguinte)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0"))
	float CoarticulationTime = 0.08f;

	// Variação aleatória (±) da força de cada visema, para a fala não parecer mecânica
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0", ClampMax = "0.5"))
	float ShapeVariation = 0.12f;

	// Velocidade de transição dos lábios e da língua
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0"))
	float SmoothingSpeed = 18.0f;

	// Velocidade de transição da mandíbula (mais pesada, mais lenta que os lábios)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0"))
	float JawSmoothingSpeed = 12.0f;

	// Abertura da mandíbula suavizada (0..1), para ler no AnimBP
	UPROPERTY(BlueprintReadOnly, Category = "Voice|LipSync")
	float MouthOpen = 0.0f;

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnPatientSpeechStarted OnSpeechStarted;

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnPatientSpeechFinished OnSpeechFinished;

	// Toca a fala pelo nome da linha no DataTable (ex.: P2_nome_data)
	UFUNCTION(BlueprintCallable, Category = "Voice")
	bool SpeakLine(FName LineId);

	// Toca um som avulso; a boca segue o envelope do áudio
	UFUNCTION(BlueprintCallable, Category = "Voice")
	bool SpeakSound(USoundBase* Sound, FName LineId);

	UFUNCTION(BlueprintCallable, Category = "Voice")
	void StopSpeaking();

	UFUNCTION(BlueprintPure, Category = "Voice")
	bool IsSpeaking() const;

	UFUNCTION(BlueprintPure, Category = "Voice")
	FText GetLineText(FName LineId) const;

	// Fala a linha cuja coluna Disparo é igual ao texto; se o paciente já estiver falando, entra na fila
	UFUNCTION(BlueprintCallable, Category = "Voice")
	bool ReactToText(const FText& DialogueText);

	// Lê os textos de um widget do quiz (pergunta ou alternativa) e faz o paciente da cena reagir.
	// Ligue no BP_Quiz: no evento OnQuizAnswerSelected (AnswerWidget) e no fim de ShowQuestion.
	UFUNCTION(BlueprintCallable, Category = "Voice", meta = (DisplayName = "Patient React To Quiz Widget"))
	static bool ReactToQuizWidget(UWidget* QuizWidget);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FVisemeCue
	{
		float Time = 0.0f;
		TCHAR Shape = TEXT('X');
		float Scale = 1.0f;
	};

	enum EMouthChannel { Jaw, Pucker, Wide, LipIn, Press, UpperUp, TongueUp, NumChannels };

	bool PlayVoice(USoundBase* Sound, FName LineId, const FString& Visemes);
	void ParseVisemes(const FString& Visemes);
	static void GetShapeWeights(TCHAR Shape, float OutWeights[NumChannels]);
	void SetShapeTarget(TCHAR Shape);
	void UpdateVisemeTarget();
	void ApplyMorphs();

	UFUNCTION()
	void HandleEnvelope(const USoundWave* PlayingSoundWave, const float EnvelopeValue);

	UFUNCTION()
	void HandleAudioFinished();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceAudio;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> PreloadedSounds;

	TWeakObjectPtr<USkeletalMeshComponent> PatientMesh;
	FName CurrentLine;
	TArray<FName> PendingLines;
	bool bStopping = false;
	TArray<FVisemeCue> Cues;
	int32 CueIndex = 0;
	float SpeechTime = 0.0f;
	float Envelope = 0.0f;
	float TargetMouth[NumChannels] = {};
	float CurrentMouth[NumChannels] = {};
};
