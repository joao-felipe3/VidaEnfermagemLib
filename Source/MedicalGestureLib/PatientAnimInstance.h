// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "PatientAnimInstance.generated.h"

class UAnimSequenceBase;

// Toca a pose base do paciente (ex.: Sitted) e gira pescoço, cabeça e olhos na direção de um alvo,
// com micro movimentos de respiração/fala. Tudo em C++, sem AnimBP.
USTRUCT()
struct MEDICALGESTURELIB_API FPatientAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FPatientAnimInstanceProxy() = default;
	explicit FPatientAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	void ApplyLook(FPoseContext& Output);

	// Copiado do UPatientAnimInstance a cada quadro
	TObjectPtr<UAnimSequenceBase> BasePose = nullptr;
	FVector TargetCS = FVector::ZeroVector;
	float Engagement = 0.0f;
	float Speech = 0.0f;
	float MaxHeadYaw = 55.0f;
	float MaxHeadPitch = 20.0f;
	float MaxEyeYaw = 25.0f;
	float MaxEyePitch = 15.0f;
	float HeadSpeed = 3.5f;

	// Estado
	float PoseTime = 0.0f;
	float LifeTime = 0.0f;
	float DeltaTime = 0.0f;
	float Weight = 0.0f;
	float HeadYaw = 0.0f;
	float HeadPitch = 0.0f;
	float EyeYaw = 0.0f;
	float EyePitch = 0.0f;
	FVector2D Saccade = FVector2D::ZeroVector;
	float NextSaccade = 0.0f;
	FRandomStream Random{ 1978 };
};

UCLASS(Transient, NotBlueprintable)
class MEDICALGESTURELIB_API UPatientAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Animação que define a pose do corpo (fica em loop)
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> BasePose;

	// Ponto que o paciente olha, em coordenadas do mundo
	FVector LookTargetWorld = FVector::ZeroVector;

	// 0 = olha para frente, 1 = olha para o alvo
	float Engagement = 0.0f;

	// Abertura da boca (0..1) enquanto fala, para pequenos acenos de cabeça
	float Speech = 0.0f;

	float MaxHeadYaw = 55.0f;
	float MaxHeadPitch = 20.0f;
	float MaxEyeYaw = 25.0f;
	float MaxEyePitch = 15.0f;
	float HeadSpeed = 3.5f;

	// Rotação calculada dos olhos (graus) para direcionar os morph targets no UPatientLookAtComponent
	UPROPERTY(Transient, BlueprintReadOnly, Category = "LookAt")
	float EyeYaw = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "LookAt")
	float EyePitch = 0.0f;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FPatientAnimInstanceProxy(this); }

	friend struct FPatientAnimInstanceProxy;
};
