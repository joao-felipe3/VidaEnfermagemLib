// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientAnimInstance.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	const FName Spine1Bone = TEXT("Spine1");
	const FName Spine2Bone = TEXT("Spine2");
	const FName NeckBone = TEXT("Neck");
	const FName HeadBone = TEXT("Head");
	const FName LeftEyeBone = TEXT("LeftEye");
	const FName RightEyeBone = TEXT("RightEye");
	const FName LeftShoulderBone = TEXT("LeftShoulder");
	const FName RightShoulderBone = TEXT("RightShoulder");

	// Giro simples (Yaw e Pitch)
	FQuat LookDelta(float YawDeg, float PitchDeg, const FVector& Right)
	{
		return FQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg)) * FQuat(Right, FMath::DegreesToRadians(PitchDeg));
	}

	// Giro completo 3-DOF com rolagem frontal (Head tilt natural para escuta e conversação)
	FQuat LookDelta(float YawDeg, float PitchDeg, float RollDeg, const FVector& Forward, const FVector& Right)
	{
		return FQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg))
			* FQuat(Right, FMath::DegreesToRadians(PitchDeg))
			* FQuat(Forward, FMath::DegreesToRadians(RollDeg));
	}
}

void FPatientAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	UPatientAnimInstance* Instance = CastChecked<UPatientAnimInstance>(InAnimInstance);
	BasePose = Instance->BasePose;
	Engagement = Instance->Engagement;
	Speech = Instance->Speech;
	MaxHeadYaw = Instance->MaxHeadYaw;
	MaxHeadPitch = Instance->MaxHeadPitch;
	MaxEyeYaw = Instance->MaxEyeYaw;
	MaxEyePitch = Instance->MaxEyePitch;
	HeadSpeed = Instance->HeadSpeed;
	if (const USkeletalMeshComponent* Mesh = Instance->GetSkelMeshComponent())
	{
		TargetCS = Mesh->GetComponentTransform().InverseTransformPosition(Instance->LookTargetWorld);
	}

	// Transmite a rotação ocular computada para ser consumida pelos morph targets no LookAtComponent
	Instance->EyeYaw = EyeYaw;
	Instance->EyePitch = EyePitch;
}

void FPatientAnimInstanceProxy::Update(float DeltaSeconds)
{
	FAnimInstanceProxy::Update(DeltaSeconds);

	DeltaTime = DeltaSeconds;
	LifeTime += DeltaSeconds;
	if (BasePose)
	{
		const float Length = BasePose->GetPlayLength();
		PoseTime = Length > 0.0f ? FMath::Fmod(PoseTime + DeltaSeconds, Length) : 0.0f;
	}

	// Movimentos sacádicos sociais e desvio reflexivo cognitivo (Cognitive Gaze Aversion):
	// - Quando falando: desvia o olhar ~35% das vezes enquanto formula pensamento clínico ou relata sintomas
	// - Quando escutando: alterna fovealmente entre olho esquerdo, olho direito e leitura labial do estudante
	if (LifeTime >= NextSaccade)
	{
		static int32 SaccadeTargetIndex = 0;
		SaccadeTargetIndex = (SaccadeTargetIndex + 1) % 5;

		const bool bIsSpeaking = Speech > 0.04f;

		if (bIsSpeaking && SaccadeTargetIndex == 4)
		{
			// Desvio cognitivo ao formular pensamento durante a fala (olhar reflexivo lateral)
			const float AversionSign = (Random.FRand() < 0.5f) ? -1.0f : 1.0f;
			Saccade = FVector2D(AversionSign * Random.FRandRange(7.5f, 11.5f), Random.FRandRange(-3.5f, 2.0f));
			NextSaccade = LifeTime + Random.FRandRange(0.8f, 1.4f);
		}
		else
		{
			switch (SaccadeTargetIndex)
			{
			case 0:
				// Foco foveal no olho esquerdo do estudante
				Saccade = FVector2D(-4.5f + Random.FRandRange(-0.8f, 0.8f), 0.8f + Random.FRandRange(-0.4f, 0.4f));
				NextSaccade = LifeTime + Random.FRandRange(1.6f, 2.6f);
				break;
			case 1:
				// Foco foveal no olho direito do estudante
				Saccade = FVector2D(4.5f + Random.FRandRange(-0.8f, 0.8f), 0.8f + Random.FRandRange(-0.4f, 0.4f));
				NextSaccade = LifeTime + Random.FRandRange(1.6f, 2.6f);
				break;
			case 2:
				// Leitura orofacial na boca do estudante (escuta atenta e comunicação empática)
				Saccade = FVector2D(Random.FRandRange(-1.2f, 1.2f), -4.2f + Random.FRandRange(-0.6f, 0.6f));
				NextSaccade = LifeTime + Random.FRandRange(1.2f, 2.0f);
				break;
			case 3:
			default:
				// Micro-ajuste reflexivo rápido
				Saccade = FVector2D(Random.FRandRange(-3.2f, 3.2f), Random.FRandRange(-1.0f, 1.5f));
				NextSaccade = LifeTime + Random.FRandRange(0.9f, 1.6f);
				break;
			}
		}
	}
}

bool FPatientAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (!BasePose)
	{
		Output.ResetToRefPose();
		return true;
	}

	FAnimationPoseData PoseData(Output);
	BasePose->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(PoseTime), false, FDeltaTimeRecord(), true));
	ApplyLook(Output);
	return true;
}

void FPatientAnimInstanceProxy::ApplyLook(FPoseContext& Output)
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Index = [&Bones](FName Name)
	{
		return Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(Name)));
	};
	const FCompactPoseBoneIndex Spine1 = Index(Spine1Bone);
	const FCompactPoseBoneIndex Spine2 = Index(Spine2Bone);
	const FCompactPoseBoneIndex Neck = Index(NeckBone);
	const FCompactPoseBoneIndex Head = Index(HeadBone);
	const FCompactPoseBoneIndex LeftEye = Index(LeftEyeBone);
	const FCompactPoseBoneIndex RightEye = Index(RightEyeBone);
	const FCompactPoseBoneIndex LeftShoulder = Index(LeftShoulderBone);
	const FCompactPoseBoneIndex RightShoulder = Index(RightShoulderBone);
	if (Neck == INDEX_NONE || Head == INDEX_NONE)
	{
		return;
	}
	const bool bHasEyes = LeftEye != INDEX_NONE && RightEye != INDEX_NONE;

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	const FTransform HeadCS = CSPose.GetComponentSpaceTransform(Head);
	const FTransform NeckCS = CSPose.GetComponentSpaceTransform(Neck);
	const FQuat NeckParentRot = CSPose.GetComponentSpaceTransform(Bones.GetParentBoneIndex(Neck)).GetRotation();

	// Frente do rosto tirada da própria pose: da cabeça para o meio dos olhos, no plano horizontal
	const FVector EyesCS = bHasEyes
		? (CSPose.GetComponentSpaceTransform(LeftEye).GetLocation() + CSPose.GetComponentSpaceTransform(RightEye).GetLocation()) * 0.5f
		: HeadCS.GetLocation() + HeadCS.GetRotation().GetAxisZ() * 10.0f;
	const FVector Forward = FVector(EyesCS - HeadCS.GetLocation()).GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		return;
	}
	const FVector Right = FVector::CrossProduct(Forward, FVector::UpVector);

	// 1. RESPIRAÇÃO TORÁCICA E CLAVICULAR (~0.23 Hz, ~14 rpm com suspiros periódicos)
	const float BreathPhase = LifeTime * 1.45f;
	const float BreathSine = FMath::Sin(BreathPhase);
	const float BreathNorm = (BreathSine + 1.0f) * 0.5f;
	const float BreathCurve = FMath::Pow(BreathNorm, 1.4f) * 2.0f - 1.0f;

	// Suspiro profundo periódico (a cada ~28 segundos)
	const float SighCycle = FMath::Sin(LifeTime * 0.22f);
	const float SighFactor = (SighCycle > 0.82f) ? (1.0f + 0.95f * (SighCycle - 0.82f) / 0.18f) : 1.0f;

	// 2. REAJUSTE POSTURAL LENTO (Micro-shift de peso na cadeira a cada 30-45s para eliminar postura congelada)
	const float PosturePhase = LifeTime * 0.08f;
	const float PostureRoll = 0.55f * FMath::Sin(PosturePhase) * FMath::Cos(PosturePhase * 0.6f);
	const float PosturePitch = 0.40f * FMath::Sin(PosturePhase * 0.7f + 0.8f);

	// 3. GESTOS CLAVICULARES CONVERSACIONAIS (Micro-elevação de ombros em ênfase de fala)
	const float SpeechShrug = FMath::Clamp(Speech * 0.9f, 0.0f, 1.0f) * (0.45f + 0.25f * FMath::Sin(LifeTime * 3.8f));

	const float TotalSpinePitch = (BreathCurve * 0.85f * SighFactor) + PosturePitch;
	const float TotalSpineRoll = PostureRoll;
	const float TotalBreathZ = ((BreathCurve + 1.0f) * 0.22f * SighFactor) + (SpeechShrug * 0.20f);

	if (Spine1 != INDEX_NONE && Spine2 != INDEX_NONE)
	{
		const FQuat SpineDelta = LookDelta(0.0f, TotalSpinePitch * 0.5f, TotalSpineRoll * 0.5f, Forward, Right);

		const FQuat Spine1ParentRot = CSPose.GetComponentSpaceTransform(Bones.GetParentBoneIndex(Spine1)).GetRotation();
		FTransform& Spine1Local = Output.Pose[Spine1];
		Spine1Local.SetRotation((Spine1ParentRot.Inverse() * SpineDelta * Spine1ParentRot * Spine1Local.GetRotation()).GetNormalized());

		const FQuat Spine2ParentRot = CSPose.GetComponentSpaceTransform(Bones.GetParentBoneIndex(Spine2)).GetRotation();
		FTransform& Spine2Local = Output.Pose[Spine2];
		Spine2Local.SetRotation((Spine2ParentRot.Inverse() * SpineDelta * Spine2ParentRot * Spine2Local.GetRotation()).GetNormalized());
		Spine2Local.AddToTranslation(Spine2ParentRot.Inverse().RotateVector(FVector(0.0f, 0.0f, TotalBreathZ)));
	}

	if (LeftShoulder != INDEX_NONE && RightShoulder != INDEX_NONE)
	{
		const float ShoulderLiftZ = ((BreathCurve + 1.0f) * 0.16f * SighFactor) + (SpeechShrug * 0.22f);
		const float ClaviclePitch = (BreathCurve * 0.60f * SighFactor) + (SpeechShrug * 0.75f);

		const FQuat LeftShoulderParentRot = CSPose.GetComponentSpaceTransform(Bones.GetParentBoneIndex(LeftShoulder)).GetRotation();
		FTransform& LeftShoulderLocal = Output.Pose[LeftShoulder];
		const FQuat ShoulderDeltaL = LookDelta(0.0f, ClaviclePitch * 0.6f, Right);
		LeftShoulderLocal.SetRotation((LeftShoulderParentRot.Inverse() * ShoulderDeltaL * LeftShoulderParentRot * LeftShoulderLocal.GetRotation()).GetNormalized());
		LeftShoulderLocal.AddToTranslation(LeftShoulderParentRot.Inverse().RotateVector(FVector(0.0f, 0.0f, ShoulderLiftZ)));

		const FQuat RightShoulderParentRot = CSPose.GetComponentSpaceTransform(Bones.GetParentBoneIndex(RightShoulder)).GetRotation();
		FTransform& RightShoulderLocal = Output.Pose[RightShoulder];
		const FQuat ShoulderDeltaR = LookDelta(0.0f, ClaviclePitch * 0.6f, Right);
		RightShoulderLocal.SetRotation((RightShoulderParentRot.Inverse() * ShoulderDeltaR * RightShoulderParentRot * RightShoulderLocal.GetRotation()).GetNormalized());
		RightShoulderLocal.AddToTranslation(RightShoulderParentRot.Inverse().RotateVector(FVector(0.0f, 0.0f, ShoulderLiftZ)));
	}

	// Ângulos até o alvo; atrás do paciente ele desiste de olhar
	const FVector Dir = (TargetCS - EyesCS).GetSafeNormal();
	const float F = FVector::DotProduct(Dir, Forward);
	const float R = FVector::DotProduct(Dir, Right);
	const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(-R, F));
	const float TargetPitch = FMath::RadiansToDegrees(FMath::Atan2(Dir.Z, FMath::Sqrt(F * F + R * R)));
	const float WantedWeight = FMath::Abs(TargetYaw) < 110.0f ? Engagement : 0.0f;
	Weight = FMath::FInterpTo(Weight, WantedWeight, DeltaTime, 2.5f);

	// Cabeça segue devagar; os olhos chegam antes e cobrem o que a cabeça não alcança
	const float WantedHeadYaw = FMath::Clamp(TargetYaw, -MaxHeadYaw, MaxHeadYaw) * Weight;
	const float WantedHeadPitch = FMath::Clamp(TargetPitch, -MaxHeadPitch, MaxHeadPitch) * Weight;
	HeadYaw = FMath::FInterpTo(HeadYaw, WantedHeadYaw, DeltaTime, HeadSpeed);
	HeadPitch = FMath::FInterpTo(HeadPitch, WantedHeadPitch, DeltaTime, HeadSpeed);
	// Micro-tremor nistágmico fisiológico contínuo (~1.4 Hz, micromovimento essencial para vida foveal)
	const float EyeTremorX = 0.35f * FMath::Sin(LifeTime * 8.8f) + 0.15f * FMath::Sin(LifeTime * 23.0f);
	const float EyeTremorY = 0.25f * FMath::Cos(LifeTime * 11.3f);

	const float WantedEyeYaw = FMath::Clamp(TargetYaw * Weight - HeadYaw, -MaxEyeYaw, MaxEyeYaw) + (Saccade.X + EyeTremorX) * Weight;
	const float WantedEyePitch = FMath::Clamp(TargetPitch * Weight - HeadPitch, -MaxEyePitch, MaxEyePitch) + (Saccade.Y + EyeTremorY) * Weight;
	EyeYaw = FMath::FInterpTo(EyeYaw, WantedEyeYaw, DeltaTime, 28.0f);
	EyePitch = FMath::FInterpTo(EyePitch, WantedEyePitch, DeltaTime, 28.0f);

	// Vida: balanço leve o tempo todo, acenos curtos acompanhando a fala e inclinação lateral (head tilt)
	const float IdleYaw = 1.3f * FMath::Sin(LifeTime * 0.37f) + 0.6f * FMath::Sin(LifeTime * 0.89f);
	const float IdlePitch = 0.8f * FMath::Sin(LifeTime * 0.53f);
	const float IdleRoll = 1.2f * FMath::Sin(LifeTime * 0.29f + 0.5f);

	// Gesticulação conversacional ativa:
	// - Aceno suave de cabeça (nod afirmativo e ênfase silábica)
	const float TalkPitch = -2.4f * Speech + 1.0f * Speech * FMath::Sin(LifeTime * 4.6f);
	const float TalkYaw = 1.2f * Speech * FMath::Sin(LifeTime * 1.5f);
	// - Inclinação lateral de escuta/atenção (head tilt típico de jovem de 24 anos ao conversar)
	const float TalkRoll = 1.6f * Speech * FMath::Sin(LifeTime * 1.1f + 0.8f);

	const float TotalYaw = HeadYaw + IdleYaw + TalkYaw;
	const float TotalPitch = HeadPitch + IdlePitch + TalkPitch;
	const float TotalRoll = IdleRoll + TalkRoll;

	// 40% do giro no pescoço e 60% na cabeça
	const FQuat NeckDelta = LookDelta(TotalYaw * 0.4f, TotalPitch * 0.4f, TotalRoll * 0.4f, Forward, Right);
	const FQuat HeadDelta = LookDelta(TotalYaw * 0.6f, TotalPitch * 0.6f, TotalRoll * 0.6f, Forward, Right);

	FTransform& NeckLocal = Output.Pose[Neck];
	NeckLocal.SetRotation((NeckParentRot.Inverse() * NeckDelta * NeckParentRot * NeckLocal.GetRotation()).GetNormalized());

	const FQuat NewNeckRot = NeckDelta * NeckCS.GetRotation();
	FTransform& HeadLocal = Output.Pose[Head];
	HeadLocal.SetRotation((NewNeckRot.Inverse() * HeadDelta * NewNeckRot * HeadLocal.GetRotation()).GetNormalized());

	if (bHasEyes)
	{
		const FQuat NewHeadRot = HeadDelta * NeckDelta * HeadCS.GetRotation();
		const FQuat EyeDelta = LookDelta(EyeYaw, EyePitch, Right);
		for (const FCompactPoseBoneIndex Eye : { LeftEye, RightEye })
		{
			FTransform& EyeLocal = Output.Pose[Eye];
			EyeLocal.SetRotation((NewHeadRot.Inverse() * EyeDelta * NewHeadRot * EyeLocal.GetRotation()).GetNormalized());
		}
	}
}
