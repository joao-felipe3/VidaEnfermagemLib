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

	return PlayVoice(Line->Som.LoadSynchronous(), LineId, Line->Visemas);
}

bool UPatientVoiceComponent::SpeakSound(USoundBase* Sound, FName LineId)
{
	return PlayVoice(Sound, LineId, FString());
}

bool UPatientVoiceComponent::PlayVoice(USoundBase* Sound, FName LineId, const FString& Visemes)
{
	if (!Sound || !VoiceAudio)
	{
		UE_LOG(LogTemp, Warning, TEXT("PatientVoice: som inválido para %s"), *LineId.ToString());
		return false;
	}

	StopSpeaking();

	CurrentLine = LineId;
	ParseVisemes(Visemes);
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
	Cues.Reset();
	SetShapeTarget(TEXT('X'));
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

void UPatientVoiceComponent::ParseVisemes(const FString& Visemes)
{
	Cues.Reset();
	CueIndex = 0;
	SpeechTime = 0.0f;

	TArray<FString> Entries;
	Visemes.ParseIntoArray(Entries, TEXT(";"));
	for (const FString& Entry : Entries)
	{
		FString TimeText, ShapeText;
		if (Entry.Split(TEXT(":"), &TimeText, &ShapeText) && !ShapeText.IsEmpty())
		{
			Cues.Add({ FCString::Atof(*TimeText), ShapeText[0] });
		}
	}
}

void UPatientVoiceComponent::SetShapeTarget(TCHAR Shape)
{
	// Peso de JawOpen, MouthPucker, MouthWide e LowerLipIn para cada forma do Rhubarb:
	// A = M/B/P, B = K/S/T/I, C = E, D = A, E = O, F = U/W, G = F/V, H = L, X = repouso
	static const TMap<TCHAR, TArray<float>> Shapes = {
		{ TEXT('A'), { 0.00f, 0.00f, 0.00f, 0.00f } },
		{ TEXT('B'), { 0.12f, 0.00f, 0.35f, 0.00f } },
		{ TEXT('C'), { 0.35f, 0.00f, 0.25f, 0.00f } },
		{ TEXT('D'), { 0.70f, 0.00f, 0.10f, 0.00f } },
		{ TEXT('E'), { 0.30f, 0.45f, 0.00f, 0.00f } },
		{ TEXT('F'), { 0.10f, 0.90f, 0.00f, 0.00f } },
		{ TEXT('G'), { 0.08f, 0.00f, 0.10f, 0.90f } },
		{ TEXT('H'), { 0.40f, 0.00f, 0.10f, 0.00f } },
	};

	const TArray<float>* Weights = Shapes.Find(Shape);
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		TargetMouth[Channel] = Weights ? (*Weights)[Channel] * MouthIntensity : 0.0f;
	}
}

void UPatientVoiceComponent::HandleEnvelope(const USoundWave* PlayingSoundWave, const float EnvelopeValue)
{
	if (Cues.Num() == 0 && IsSpeaking())
	{
		TargetMouth[Jaw] = FMath::Clamp(EnvelopeValue * EnvelopeGain, 0.0f, 1.0f) * MouthIntensity;
	}
}

void UPatientVoiceComponent::HandleAudioFinished()
{
	// Uma fala nova já começou; o fim da anterior chegou atrasado
	if (VoiceAudio->IsPlaying())
	{
		return;
	}

	Cues.Reset();
	SetShapeTarget(TEXT('X'));
	const FName FinishedLine = CurrentLine;
	CurrentLine = NAME_None;
	OnSpeechFinished.Broadcast(FinishedLine);
}

void UPatientVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Cues.Num() > 0 && IsSpeaking())
	{
		SpeechTime += DeltaTime;
		const float VisemeTime = SpeechTime - VisemeTimeOffset;
		while (CueIndex + 1 < Cues.Num() && Cues[CueIndex + 1].Time <= VisemeTime)
		{
			++CueIndex;
		}
		SetShapeTarget(Cues[CueIndex].Shape);
	}

	bool bMoving = IsSpeaking();
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		CurrentMouth[Channel] = FMath::FInterpTo(CurrentMouth[Channel], TargetMouth[Channel], DeltaTime, SmoothingSpeed);
		if (CurrentMouth[Channel] < KINDA_SMALL_NUMBER && TargetMouth[Channel] == 0.0f)
		{
			CurrentMouth[Channel] = 0.0f;
		}
		bMoving |= CurrentMouth[Channel] > 0.0f;
	}
	MouthOpen = CurrentMouth[Jaw];

	ApplyMorphs();

	if (!bMoving)
	{
		SetComponentTickEnabled(false);
	}
}

void UPatientVoiceComponent::ApplyMorphs()
{
	USkeletalMeshComponent* Mesh = PatientMesh.Get();
	if (!Mesh)
	{
		return;
	}

	const FName Morphs[NumChannels] = { JawOpenMorph, MouthPuckerMorph, MouthWideMorph, LowerLipInMorph };
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		if (!Morphs[Channel].IsNone())
		{
			Mesh->SetMorphTarget(Morphs[Channel], CurrentMouth[Channel]);
		}
	}
}
