// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PatientLookAtComponent.generated.h"

class UAnimSequenceBase;
class UPatientAnimInstance;
class UPatientVoiceComponent;
class USkeletalMeshComponent;

// Faz o paciente olhar para o estudante (cabeça e olhos) quando ele se aproxima ou quando o paciente
// fala, com pequenos movimentos de cabeça para não parecer um boneco estático. Também anima o rosto:
// piscadas, sobrancelhas que acompanham a fala, leve preocupação de fundo e sorriso ao ser abordado.
//
// No BeginPlay troca a animação da malha por UPatientAnimInstance, que toca a mesma pose base
// (a "Anim to Play" configurada na cena, ex.: Sitted) e aplica o olhar por cima.
UCLASS(ClassGroup = (Animation), meta = (BlueprintSpawnableComponent))
class MEDICALGESTURELIB_API UPatientLookAtComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPatientLookAtComponent();

	// Pose do corpo; vazio = usa a "Anim to Play" da malha (modo Use Animation Asset)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt")
	TObjectPtr<UAnimSequenceBase> BasePose;

	// Distância (cm) em que o estudante "começa a interação" e o paciente passa a olhar para ele
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt", meta = (ClampMin = "0"))
	float EngageDistance = 250.0f;

	// Olhar para o estudante sempre que o paciente estiver falando, mesmo de longe
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt")
	bool bLookWhileSpeaking = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Limits", meta = (ClampMin = "0", ClampMax = "80"))
	float MaxHeadYaw = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Limits", meta = (ClampMin = "0", ClampMax = "45"))
	float MaxHeadPitch = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Limits", meta = (ClampMin = "0", ClampMax = "35"))
	float MaxEyeYaw = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Limits", meta = (ClampMin = "0", ClampMax = "25"))
	float MaxEyePitch = 15.0f;

	// Rapidez com que a cabeça vira (maior = mais rápido)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt", meta = (ClampMin = "0.5"))
	float HeadSpeed = 3.5f;

	// Morph targets de expressão criados por Tools/LipSync/criar_visemas_remy.py; vazio = ignora
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName BlinkMorph = TEXT("Blink");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName BrowUpMorph = TEXT("BrowUp");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName BrowDownMorph = TEXT("BrowDown");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName SmileMorph = TEXT("Smile");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName SquintMorph = TEXT("Squint");

	// Morph targets de musculatura facial anatômica (FACS AU)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName BrowInnerUpMorph = TEXT("BrowInnerUp");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName MouthFrownMorph = TEXT("MouthFrown");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName NoseWrinkleMorph = TEXT("NoseWrinkle");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName ChinRaiseMorph = TEXT("ChinRaise");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName CheekCompressMorph = TEXT("CheekCompress");

	// Morph targets de rotação dos olhos (para rigs sem ossos de olho como Mixamo)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName EyeLookLeftMorph = TEXT("EyeLookLeft");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName EyeLookRightMorph = TEXT("EyeLookRight");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName EyeLookUpMorph = TEXT("EyeLookUp");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FName EyeLookDownMorph = TEXT("EyeLookDown");

	// Intervalo aleatório entre piscadas, em segundos (pessoas piscam a cada 2-6 s)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (ClampMin = "0.5"))
	FVector2D BlinkInterval = FVector2D(2.0f, 5.0f);

	// Sorriso no repouso: 0.0 mantém a boca 100% natural, relaxada e anatômica
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (ClampMin = "0", ClampMax = "1"))
	float BaseSmile = 0.0f;

	// Preocupação de fundo (sobrancelhas franzidas): leve concentração clínica natural
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (ClampMin = "0", ClampMax = "1"))
	float BaseConcern = 0.10f;

	// Sorriso educado ao ser cumprimentado pelo estudante
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (ClampMin = "0", ClampMax = "1"))
	float GreetingSmile = 0.40f;

	// Elevação sutil das sobrancelhas nos picos de fala conversacional
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (ClampMin = "0", ClampMax = "1"))
	float SpeechBrowRaise = 0.25f;

	// Força uma expressão por alguns segundos (ex.: BrowDown 0.8 ao ver a agulha). 0..1 cada
	UFUNCTION(BlueprintCallable, Category = "Face")
	void SetExpression(float Smile, float BrowUp, float BrowDown, float Duration = 2.0f);

	// Força uma reação clínica específica (dor aguda/punção, preocupação/ansiedade, dúvida/hesitação). 0..1 cada
	UFUNCTION(BlueprintCallable, Category = "Face")
	void SetClinicalReaction(float InPain, float InConcern, float InHesitation, float Duration = 2.0f);

	// Pisca agora (ex.: ao ser surpreendido)
	UFUNCTION(BlueprintCallable, Category = "Face")
	void Blink();

	// Força o olhar ligado ou desligado (ex.: durante a coleta, olhar para o braço)
	UFUNCTION(BlueprintCallable, Category = "LookAt")
	void SetLookOverride(bool bEnable, bool bLook);

	// Olha para um ponto do mundo em vez do estudante; ZeroVector volta para o estudante
	UFUNCTION(BlueprintCallable, Category = "LookAt")
	void SetLookTarget(FVector WorldLocation);

	UFUNCTION(BlueprintPure, Category = "LookAt")
	bool IsEngaged() const { return Engagement > 0.5f; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void UpdateFace(float DeltaTime, bool bSpeaking, float Speech);

	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<UPatientVoiceComponent> Voice;
	TWeakObjectPtr<UPatientAnimInstance> AnimInstance;
	FVector CustomTarget = FVector::ZeroVector;
	bool bOverride = false;
	bool bOverrideLook = false;
	float Engagement = 0.0f;

	// Rosto
	float BlinkTime = -1.0f;      // tempo desde o início da piscada atual (<0 = sem piscada)
	float NextBlink = 0.0f;
	bool bDoubleBlink = false;
	float GreetTime = -1.0f;      // tempo desde que o estudante chegou perto
	float ExpressionTime = 0.0f;  // segundos restantes da expressão forçada
	FVector ForcedExpression = FVector::ZeroVector;  // Smile, BrowUp, BrowDown
	float ClinicalReactionTime = 0.0f;
	float ForcedPain = 0.0f;
	float ForcedConcern = 0.0f;
	float ForcedHesitation = 0.0f;

	float Smile = 0.0f;
	float BrowUp = 0.0f;
	float BrowDown = 0.0f;
	float Squint = 0.0f;
	float BrowInnerUp = 0.0f;
	float MouthFrown = 0.0f;
	float NoseWrinkle = 0.0f;
	float ChinRaise = 0.0f;
	float CheekCompress = 0.0f;
	float SpeechPeak = 0.0f;
	float SpeechActivity = 0.0f;
	bool bWasSpeaking = false;
};
