// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientLookAtComponent.h"

#include "Animation/AnimSequenceBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "PatientAnimInstance.h"
#include "PatientVoiceComponent.h"

UPatientLookAtComponent::UPatientLookAtComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPatientLookAtComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	Voice = Owner->FindComponentByClass<UPatientVoiceComponent>();
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("PatientLookAt: %s não tem SkeletalMeshComponent"), *Owner->GetName());
		return;
	}

	UAnimSequenceBase* Pose = BasePose;
	if (!Pose && MeshComponent->GetAnimationMode() == EAnimationMode::AnimationSingleNode)
	{
		Pose = Cast<UAnimSequenceBase>(MeshComponent->AnimationData.AnimToPlay);
	}
	if (!Pose)
	{
		UE_LOG(LogTemp, Warning, TEXT("PatientLookAt: sem pose base (defina BasePose ou use 'Use Animation Asset' na malha); olhar desligado"));
		return;
	}

	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->SetAnimInstanceClass(UPatientAnimInstance::StaticClass());
	UPatientAnimInstance* Instance = Cast<UPatientAnimInstance>(MeshComponent->GetAnimInstance());
	if (!Instance)
	{
		return;
	}
	Instance->BasePose = Pose;
	Instance->MaxHeadYaw = MaxHeadYaw;
	Instance->MaxHeadPitch = MaxHeadPitch;
	Instance->MaxEyeYaw = MaxEyeYaw;
	Instance->MaxEyePitch = MaxEyePitch;
	Instance->HeadSpeed = HeadSpeed;
	AnimInstance = Instance;
}

void UPatientLookAtComponent::SetLookOverride(bool bEnable, bool bLook)
{
	bOverride = bEnable;
	bOverrideLook = bLook;
}

void UPatientLookAtComponent::SetLookTarget(FVector WorldLocation)
{
	CustomTarget = WorldLocation;
}

void UPatientLookAtComponent::SetExpression(float InSmile, float InBrowUp, float InBrowDown, float Duration)
{
	ForcedExpression = FVector(InSmile, InBrowUp, InBrowDown);
	ExpressionTime = Duration;
}

void UPatientLookAtComponent::Blink()
{
	if (BlinkTime < 0.0f)
	{
		BlinkTime = 0.0f;
	}
}

void UPatientLookAtComponent::UpdateFace(float DeltaTime, bool bSpeaking, float Speech)
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();

	// Piscada: fecha em 70 ms, segura 30 ms e abre em 130 ms; às vezes pisca duas vezes
	constexpr float CloseTime = 0.07f, HoldTime = 0.03f, OpenTime = 0.13f;
	if (BlinkTime < 0.0f && Now >= NextBlink)
	{
		Blink();
	}
	float BlinkWeight = 0.0f;
	if (BlinkTime >= 0.0f)
	{
		BlinkTime += DeltaTime;
		if (BlinkTime < CloseTime)
		{
			BlinkWeight = BlinkTime / CloseTime;
		}
		else if (BlinkTime < CloseTime + HoldTime)
		{
			BlinkWeight = 1.0f;
		}
		else if (BlinkTime < CloseTime + HoldTime + OpenTime)
		{
			BlinkWeight = 1.0f - (BlinkTime - CloseTime - HoldTime) / OpenTime;
		}
		else
		{
			BlinkTime = -1.0f;
			if (!bDoubleBlink && FMath::FRand() < 0.15f)
			{
				bDoubleBlink = true;
				NextBlink = Now + 0.12f;
			}
			else
			{
				bDoubleBlink = false;
				NextBlink = Now + FMath::FRandRange(BlinkInterval.X, BlinkInterval.Y);
			}
		}
	}
	BlinkWeight = FMath::SmoothStep(0.0f, 1.0f, BlinkWeight);

	// Atividade de fala: transição suave entre estado de escuta e modo de conversação ativa
	SpeechActivity = FMath::FInterpTo(SpeechActivity, bSpeaking ? 1.0f : 0.0f, DeltaTime, 4.0f);

	// Picos de ênfase na fala (prosódia) baseados na abertura da boca / energia sonora
	const float SpeechInput = bSpeaking ? FMath::Clamp(Speech * 1.6f, 0.0f, 1.0f) : 0.0f;
	SpeechPeak = FMath::Max(SpeechPeak - DeltaTime * 2.2f, SpeechInput);

	// Ciclo de respiração e micro-movimentos orgânicos (o paciente está vivo e respira o tempo todo)
	const float BreathCycle = FMath::Sin(Now * 1.5f);
	const float BreathMicroSmile = 0.03f * BreathCycle;
	const float BreathMicroConcern = 0.04f * FMath::Sin(Now * 1.5f + 0.8f);

	// Variações de atenção e pensamento a cada 3 a 5 segundos (elimina o efeito de manequim no repouso)
	const float ThoughtWave = FMath::Sin(Now * 0.43f) * FMath::Cos(Now * 0.27f + 1.2f);
	const float IdleAttentiveLift = FMath::Clamp(ThoughtWave * 0.18f, 0.0f, 0.25f);
	const float IdleConcernShift = ThoughtWave * 0.12f;

	// Saudação inicial ao ser abordado
	if (GreetTime >= 0.0f)
	{
		GreetTime += DeltaTime;
	}
	const float Greeting = GreetTime >= 0.0f ? FMath::Clamp(1.0f - FMath::Abs(GreetTime - 1.2f) / 1.2f, 0.0f, 1.0f) : 0.0f;
	const float GreetDecay = GreetTime >= 0.0f ? FMath::Clamp(1.0f - GreetTime / 3.0f, 0.0f, 1.0f) : 0.0f;

	// 1. SORRISO E BOCHECHAS (Smile / CheekRaiser):
	// - Estado normal: repouso acolhedor e vivo (0.18) com micro-respiração e pico ao cumprimentar (0.65)
	// - Falando: bochechas sobem e flexionam dinamicamente acompanhando a fala (+0.30*Speech + 0.15*SpeechPeak)
	const float BaseSmile = Engagement * 0.18f + BreathMicroSmile + GreetingSmile * Greeting;
	const float SpeechSmileFlex = 0.30f * Speech + 0.15f * SpeechPeak;
	float WantedSmile = FMath::Clamp(BaseSmile + SpeechActivity * SpeechSmileFlex, 0.0f, 1.0f);

	// 2. SOBRANCELHAS ERGUIDAS (BrowUp):
	// - Estado normal: leve elevação atenta que varia com o fluxo de pensamentos (0..0.25) e saudação
	// - Falando: postura conversacional ativa (0.35) que acentua nos picos da voz (+SpeechBrowRaise * SpeechPeak)
	const float IdleBrowUp = FMath::Max(IdleAttentiveLift, 0.45f * Greeting);
	const float SpeechBrowUp = 0.35f * SpeechActivity + (SpeechBrowRaise * 0.8f) * SpeechPeak + 0.10f * FMath::Sin(Now * 3.5f) * Speech;
	float WantedBrowUp = FMath::Clamp(FMath::Max(IdleBrowUp, SpeechBrowUp), 0.0f, 1.0f);

	// 3. PREOCUPAÇÃO / CONCENTRAÇÃO (BrowDown):
	// - Estado normal: preocupação médica de fundo com micro-variações orgânicas
	// - Falando: o corrugador relaxa ligeiramente nos picos de BrowUp para dar expressividade elástica
	const float BaseConcernAdjusted = BaseConcern + BreathMicroConcern + IdleConcernShift;
	const float SpeakingConcernRelief = 0.25f * SpeechPeak;
	float WantedBrowDown = FMath::Clamp((BaseConcernAdjusted - SpeakingConcernRelief) * (1.0f - 0.4f * GreetDecay), 0.10f, 0.90f);

	if (ExpressionTime > 0.0f)
	{
		ExpressionTime -= DeltaTime;
		WantedSmile = ForcedExpression.X;
		WantedBrowUp = ForcedExpression.Y;
		WantedBrowDown = ForcedExpression.Z;
	}

	Smile = FMath::FInterpTo(Smile, WantedSmile, DeltaTime, 5.0f);
	BrowUp = FMath::FInterpTo(BrowUp, WantedBrowUp, DeltaTime, 8.0f);
	BrowDown = FMath::FInterpTo(BrowDown, WantedBrowDown, DeltaTime, 4.0f);

	// 4. CONTRAÇÃO OCULAR / SQUINT:
	// - Sorriso Duchenne nos cantos dos olhos + foco nos picos de fala
	const float WantedSquint = FMath::Clamp(Smile * 0.25f + BrowDown * 0.30f + 0.20f * SpeechPeak, 0.0f, 0.75f);
	Squint = FMath::FInterpTo(Squint, WantedSquint, DeltaTime, 5.0f);

	const TPair<FName, float> Morphs[] = {
		{ BlinkMorph, BlinkWeight },
		{ BrowUpMorph, BrowUp },
		{ BrowDownMorph, BrowDown },
		{ SmileMorph, Smile },
		{ SquintMorph, Squint }
	};
	for (const TPair<FName, float>& Morph : Morphs)
	{
		if (!Morph.Key.IsNone())
		{
			MeshComponent->SetMorphTarget(Morph.Key, Morph.Value);
		}
	}

	// Olhar dos olhos (Gaze tracking nos morph targets para rigs Mixamo sem ossos de olho)
	if (const UPatientAnimInstance* Instance = AnimInstance.Get())
	{
		const float CurEyeYaw = Instance->EyeYaw;
		const float CurEyePitch = Instance->EyePitch;
		const float SafeMaxYaw = FMath::Max(MaxEyeYaw, 1.0f);
		const float SafeMaxPitch = FMath::Max(MaxEyePitch, 1.0f);
		const float NormYaw = FMath::Clamp(CurEyeYaw / SafeMaxYaw, -1.0f, 1.0f);
		const float NormPitch = FMath::Clamp(CurEyePitch / SafeMaxPitch, -1.0f, 1.0f);

		// CurEyeYaw > 0 = esquerda do paciente; CurEyeYaw < 0 = direita do paciente
		if (!EyeLookLeftMorph.IsNone())
		{
			MeshComponent->SetMorphTarget(EyeLookLeftMorph, NormYaw > 0.0f ? NormYaw : 0.0f);
		}
		if (!EyeLookRightMorph.IsNone())
		{
			MeshComponent->SetMorphTarget(EyeLookRightMorph, NormYaw < 0.0f ? -NormYaw : 0.0f);
		}
		if (!EyeLookUpMorph.IsNone())
		{
			MeshComponent->SetMorphTarget(EyeLookUpMorph, NormPitch > 0.0f ? NormPitch : 0.0f);
		}
		if (!EyeLookDownMorph.IsNone())
		{
			MeshComponent->SetMorphTarget(EyeLookDownMorph, NormPitch < 0.0f ? -NormPitch : 0.0f);
		}
	}
}

void UPatientLookAtComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UPatientVoiceComponent* VoiceComponent = Voice.Get();
	const bool bSpeaking = VoiceComponent && VoiceComponent->IsSpeaking();
	const float Speech = VoiceComponent ? VoiceComponent->MouthOpen : 0.0f;
	UpdateFace(DeltaTime, bSpeaking, Speech);

	UPatientAnimInstance* Instance = AnimInstance.Get();
	const USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!Instance || !MeshComponent)
	{
		return;
	}

	// Olha para a câmera do jogador (o headset em VR), ou para um alvo definido por Blueprint
	FVector Target = CustomTarget;
	if (Target.IsZero())
	{
		const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
		if (!Camera)
		{
			return;
		}
		Target = Camera->GetCameraLocation();
	}

	const FVector Head = MeshComponent->DoesSocketExist(TEXT("Head"))
		? MeshComponent->GetSocketLocation(TEXT("Head"))
		: MeshComponent->GetComponentLocation();

	bool bLook = FVector::Dist(Head, Target) <= EngageDistance || (bLookWhileSpeaking && bSpeaking);
	if (bOverride)
	{
		bLook = bOverrideLook;
	}
	if (bLook && Engagement < 0.5f)
	{
		// Começou a interação: cumprimenta com o rosto e pisca ao virar a cabeça, como as pessoas fazem
		GreetTime = 0.0f;
		Blink();
	}
	Engagement = bLook ? 1.0f : 0.0f;

	Instance->LookTargetWorld = Target;
	Instance->Engagement = Engagement;
	Instance->Speech = Speech;
}
