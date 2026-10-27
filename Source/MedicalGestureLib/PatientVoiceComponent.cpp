// Fill out your copyright notice in the Description page of Project Settings.


#include "PatientVoiceComponent.h"

#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

UPatientVoiceComponent::UPatientVoiceComponent()
{
	// Só precisa de tick enquanto a boca estiver se movendo
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPatientVoiceComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	PatientMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();

	VoiceAudio = NewObject<UAudioComponent>(Owner, TEXT("PatientVoiceAudio"));
	VoiceAudio->bAutoActivate = false;
	VoiceAudio->bAutoDestroy = false;
	VoiceAudio->bAllowSpatialization = true;
	VoiceAudio->AttenuationSettings = Attenuation;
	if (PatientMesh.IsValid())
	{
		VoiceAudio->SetupAttachment(PatientMesh.Get(), HeadBoneName);
	}
	else
	{
		VoiceAudio->SetupAttachment(Owner->GetRootComponent());
	}
	VoiceAudio->RegisterComponent();

	// O envelope só é calculado se o delegate estiver ligado antes do Play
	VoiceAudio->OnAudioSingleEnvelopeValue.AddDynamic(this, &UPatientVoiceComponent::HandleEnvelope);
	VoiceAudio->OnAudioFinished.AddDynamic(this, &UPatientVoiceComponent::HandleAudioFinished);

	if (bPreloadLines && VoiceLines)
	{
		VoiceLines->ForeachRow<FPatientVoiceLine>(TEXT("PatientVoice Preload"), [this](const FName&, const FPatientVoiceLine& Line)
		{
			if (USoundBase* Sound = Line.Som.LoadSynchronous())
			{
				PreloadedSounds.Add(Sound);
			}
		});
	}
}

bool UPatientVoiceComponent::SpeakLine(FName LineId)
{
	if (!VoiceLines)
	{
		UE_LOG(LogTemp, Warning, TEXT("PatientVoice: VoiceLines não definido"));
		return false;
	}

	const FPatientVoiceLine* Line = VoiceLines->FindRow<FPatientVoiceLine>(LineId, TEXT("PatientVoice SpeakLine"));
	if (!Line)
	{
		return false;
	}

	return SpeakSound(Line->Som.LoadSynchronous(), LineId);
}

bool UPatientVoiceComponent::SpeakSound(USoundBase* Sound, FName LineId)
{
	if (!Sound || !VoiceAudio)
	{
		UE_LOG(LogTemp, Warning, TEXT("PatientVoice: som inválido para %s"), *LineId.ToString());
		return false;
	}

	StopSpeaking();

	CurrentLine = LineId;
	VoiceAudio->SetSound(Sound);
	VoiceAudio->Play();
	SetComponentTickEnabled(true);

	OnSpeechStarted.Broadcast(LineId, GetLineText(LineId));
	return true;
}

void UPatientVoiceComponent::StopSpeaking()
{
	if (VoiceAudio && VoiceAudio->IsPlaying())
	{
		VoiceAudio->Stop();
	}
	TargetMouthOpen = 0.0f;
}

bool UPatientVoiceComponent::IsSpeaking() const
{
	return VoiceAudio && VoiceAudio->IsPlaying();
}

FText UPatientVoiceComponent::GetLineText(FName LineId) const
{
	if (VoiceLines)
	{
		if (const FPatientVoiceLine* Line = VoiceLines->FindRow<FPatientVoiceLine>(LineId, TEXT("PatientVoice GetLineText"), false))
		{
			return Line->Texto;
		}
	}
	return FText::GetEmpty();
}

void UPatientVoiceComponent::HandleEnvelope(const USoundWave* PlayingSoundWave, const float EnvelopeValue)
{
	TargetMouthOpen = FMath::Clamp(EnvelopeValue * EnvelopeGain, 0.0f, 1.0f);
}

void UPatientVoiceComponent::HandleAudioFinished()
{
	// Uma fala nova já começou; o fim da anterior chegou atrasado
	if (VoiceAudio->IsPlaying())
	{
		return;
	}

	TargetMouthOpen = 0.0f;
	const FName FinishedLine = CurrentLine;
	CurrentLine = NAME_None;
	OnSpeechFinished.Broadcast(FinishedLine);
}

void UPatientVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	MouthOpen = FMath::FInterpTo(MouthOpen, TargetMouthOpen, DeltaTime, SmoothingSpeed);

	if (!IsSpeaking() && MouthOpen < KINDA_SMALL_NUMBER)
	{
		MouthOpen = 0.0f;
		SetComponentTickEnabled(false);
	}

	if (!MouthMorphTarget.IsNone() && PatientMesh.IsValid())
	{
		PatientMesh->SetMorphTarget(MouthMorphTarget, MouthOpen);
	}
}
