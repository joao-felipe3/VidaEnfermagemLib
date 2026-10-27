// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PatientVoiceComponent.generated.h"

class UAudioComponent;
class USkeletalMeshComponent;
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
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPatientSpeechStarted, FName, LineId, const FText&, Texto);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPatientSpeechFinished, FName, LineId);

// Toca as falas do paciente a partir da cabeça (áudio espacial) e gera um valor de abertura de boca
// a partir do envelope do áudio, para morph target ou para o AnimBP (osso da mandíbula/cabeça).
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Spatial")
	TObjectPtr<USoundAttenuation> Attenuation;

	// Osso/socket do paciente de onde a voz sai
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Spatial")
	FName HeadBoneName = TEXT("Head");

	// Morph target da boca; deixe vazio se a malha não tiver (use MouthOpen no AnimBP)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync")
	FName MouthMorphTarget = NAME_None;

	// Multiplicador do envelope do áudio (fala costuma ficar entre 0.05 e 0.3)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0"))
	float EnvelopeGain = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|LipSync", meta = (ClampMin = "0"))
	float SmoothingSpeed = 18.0f;

	// Abertura da boca suavizada (0..1), para ler no AnimBP
	UPROPERTY(BlueprintReadOnly, Category = "Voice|LipSync")
	float MouthOpen = 0.0f;

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnPatientSpeechStarted OnSpeechStarted;

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnPatientSpeechFinished OnSpeechFinished;

	// Toca a fala pelo nome da linha no DataTable (ex.: P2_nome_data)
	UFUNCTION(BlueprintCallable, Category = "Voice")
	bool SpeakLine(FName LineId);

	UFUNCTION(BlueprintCallable, Category = "Voice")
	bool SpeakSound(USoundBase* Sound, FName LineId);

	UFUNCTION(BlueprintCallable, Category = "Voice")
	void StopSpeaking();

	UFUNCTION(BlueprintPure, Category = "Voice")
	bool IsSpeaking() const;

	UFUNCTION(BlueprintPure, Category = "Voice")
	FText GetLineText(FName LineId) const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
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
	float TargetMouthOpen = 0.0f;
};
