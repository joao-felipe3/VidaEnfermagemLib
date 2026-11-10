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

	// Disparo de piscada orgânica no início de uma fala (humanos quase sempre piscam ao começar uma oração)
	if (bSpeaking && !bWasSpeaking && BlinkTime < 0.0f)
	{
		Blink();
	}
	bWasSpeaking = bSpeaking;

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
				const float IntervalMin = bSpeaking ? BlinkInterval.X * 0.70f : BlinkInterval.X;
				const float IntervalMax = bSpeaking ? BlinkInterval.Y * 0.85f : BlinkInterval.Y;
				NextBlink = Now + FMath::FRandRange(IntervalMin, IntervalMax);
			}
		}
	}
	BlinkWeight = FMath::SmoothStep(0.0f, 1.0f, BlinkWeight);

	// Atividade de fala: transição suave entre estado de escuta e modo de conversação ativa
	SpeechActivity = FMath::FInterpTo(SpeechActivity, bSpeaking ? 1.0f : 0.0f, DeltaTime, 4.0f);

	// Picos de ênfase na fala (prosódia) baseados na abertura da boca / energia sonora
	const float SpeechInput = bSpeaking ? FMath::Clamp(Speech * 1.2f, 0.0f, 1.0f) : 0.0f;
	SpeechPeak = FMath::Max(SpeechPeak - DeltaTime * 2.5f, SpeechInput);

	// Ciclo de respiração e micro-movimentos orgânicos (o paciente está vivo e respira o tempo todo)
	const float BreathCycle = FMath::Sin(Now * 1.5f);
	const float BreathMicroSmile = 0.015f * BreathCycle;
	const float BreathMicroConcern = 0.02f * FMath::Sin(Now * 1.5f + 0.8f);

	// Variações de atenção e pensamento a cada 3 a 5 segundos (elimina o efeito de manequim no repouso)
	const float ThoughtWave = FMath::Sin(Now * 0.43f) * FMath::Cos(Now * 0.27f + 1.2f);
	const float IdleAttentiveLift = FMath::Clamp(ThoughtWave * 0.15f, 0.0f, 0.20f);
	const float IdleConcernShift = ThoughtWave * 0.08f;
	const float IdleSmileShift = ThoughtWave * 0.03f;

	// Saudação inicial ao ser abordado:
	// - Sobe nos primeiros 0.6s, mantém sustentado até 2.0s, e transiciona suavemente para o BaseSmile em 3.5s
	float Greeting = 0.0f;
	if (GreetTime >= 0.0f)
	{
		GreetTime += DeltaTime;
		if (GreetTime < 0.6f)
		{
			Greeting = GreetTime / 0.6f;
		}
		else if (GreetTime < 2.0f)
		{
			Greeting = 1.0f;
		}
		else if (GreetTime < 3.5f)
		{
			Greeting = 1.0f - (GreetTime - 2.0f) / 1.5f;
		}
		else
		{
			Greeting = 0.0f;
		}
	}

	// 1. SORRISO E BOCHECHAS (Smile / CheekRaiser):
	// - Estado normal: repouso sutil e natural (BaseSmile ~ 0.08, lábios selados), com acolhimento educado ao cumprimentar (Greeting ~ 0.35)
	// - Falando: bochechas flexionam sutilmente com a articulação (+0.10*Speech + 0.08*SpeechPeak)
	const float RestSmile = Engagement * (BaseSmile + IdleSmileShift) + BreathMicroSmile + GreetingSmile * Greeting;
	const float SpeechSmileMod = 0.10f * Speech + 0.08f * SpeechPeak;
	float WantedSmile = FMath::Clamp(RestSmile + SpeechActivity * SpeechSmileMod, 0.0f, 0.40f);

	// 2. SOBRANCELHAS ERGUIDAS (BrowUp):
	// - Estado normal: sobrancelhas relaxadas no repouso com micro-elevação atenta viva (ThoughtWave)
	// - Saudação: arqueamento caloroso e acolhedor (0.42 * Greeting)
	// - Falando: acentuação conversacional elegante e legível (0.22*SpeechActivity + SpeechBrowRaise*SpeechPeak)
	const float IdleBrowUp = FMath::Max(IdleAttentiveLift, 0.42f * Greeting);
	const float SpeechBrowUp = 0.22f * SpeechActivity + (SpeechBrowRaise * 0.90f) * SpeechPeak + 0.08f * FMath::Sin(Now * 3.5f) * Speech;
	float WantedBrowUp = FMath::Clamp(FMath::Max(IdleBrowUp, SpeechBrowUp), 0.0f, 0.70f);

	// 3. PREOCUPAÇÃO / CONCENTRAÇÃO (BrowDown):
	// - Estado normal: leve concentração médica de fundo
	// - Ao sorrir ou cumprimentar: o músculo corrugador relaxa organicamente
	const float BaseConcernAdjusted = BaseConcern + BreathMicroConcern + IdleConcernShift;
	const float SpeakingConcernRelief = 0.12f * SpeechPeak;
	const float SmileConcernRelief = 0.28f * WantedSmile;
	float WantedBrowDown = FMath::Clamp((BaseConcernAdjusted - SpeakingConcernRelief - SmileConcernRelief) * (1.0f - 0.35f * Greeting), 0.0f, 0.65f);

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

	// 4. CONTRAÇÃO OCULAR / SQUINT (AU6 Cheek Raiser + AU7 Lid Tightener):
	// - Tônus de atenção vivo de fundo (elimina o efeito de manequim estático e olhar esbugalhado)
	const float BaseAttentiveSquint = Engagement * (0.12f + IdleAttentiveLift * 0.35f);
	// - Sorriso Duchenne autêntico nos cantos dos olhos
	const float SmileDuchenneSquint = Smile * 0.45f;
	// - Expressividade dinâmica de fala (a musculatura periocular flexiona com a ênfase silábica)
	const float SpeechSquint = 0.16f * SpeechActivity + 0.32f * SpeechPeak;
	// - Concentração / preocupação clínica
	const float ConcernSquint = BrowDown * 0.25f;

	const float WantedSquint = FMath::Clamp(BaseAttentiveSquint + SmileDuchenneSquint + SpeechSquint + ConcernSquint, 0.0f, 0.85f);
	Squint = FMath::FInterpTo(Squint, WantedSquint, DeltaTime, 6.0f);

	// 5. GAZE-EYELID SYNERGY (Inervação óculo-palpebral fisiológica de Hering):
	// Quando os olhos olham para baixo (NormPitch < 0), a pálpebra superior desce suavemente para acompanhar a córnea
	// Isso impede 100% de exposição artificial da esclera superior ("olhar arregalado/esbugalhado")
	float GazeLidFollow = 0.0f;
	if (const UPatientAnimInstance* Instance = AnimInstance.Get())
	{
		const float SafeMaxPitch = FMath::Max(MaxEyePitch, 1.0f);
		const float NormPitch = FMath::Clamp(Instance->EyePitch / SafeMaxPitch, -1.0f, 1.0f);
		if (NormPitch < 0.0f)
		{
			GazeLidFollow = -NormPitch * 0.22f;
		}
	}
	const float EffectiveBlink = FMath::Clamp(BlinkWeight + GazeLidFollow, 0.0f, 1.0f);

	const TPair<FName, float> Morphs[] = {
		{ BlinkMorph, EffectiveBlink },
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
