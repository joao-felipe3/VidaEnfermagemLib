// Fill out your copyright notice in the Description page of Project Settings.


#include "PatientVoiceComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// Compara textos do quiz ignorando aspas, maiúsculas e espaços repetidos
	FString NormalizeDialogue(const FString& Text)
	{
		FString Result;
		Result.Reserve(Text.Len());
		bool bPendingSpace = false;
		for (TCHAR Char : Text)
		{
			if (Char == TEXT('"') || Char == TEXT('\'') || Char == 0x201C || Char == 0x201D)
			{
				continue;
			}
			if (FChar::IsWhitespace(Char))
			{
				bPendingSpace = !Result.IsEmpty();
				continue;
			}
			if (bPendingSpace)
			{
				Result.AppendChar(TEXT(' '));
				bPendingSpace = false;
			}
			Result.AppendChar(FChar::ToLower(Char));
		}
		return Result;
	}
}

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
	if (!Attenuation)
	{
		// Sem atenuação o som tocaria em 2D; usa uma esfera simples a partir da cabeça
		VoiceAudio->bOverrideAttenuation = true;
		VoiceAudio->AttenuationOverrides.bAttenuate = true;
		VoiceAudio->AttenuationOverrides.bSpatialize = true;
		VoiceAudio->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
		VoiceAudio->AttenuationOverrides.AttenuationShapeExtents = FVector(150.0f, 0.0f, 0.0f);
		VoiceAudio->AttenuationOverrides.FalloffDistance = FalloffDistance;
	}
	if (PatientMesh.IsValid())
	{
		VoiceAudio->SetupAttachment(PatientMesh.Get(), HeadBoneName);
	}
	else
	{
		VoiceAudio->SetupAttachment(Owner->GetRootComponent());
	}
	VoiceAudio->RegisterComponent();

	// O envelope é calculado automaticamente pelo áudio renderer quando o delegate é registrado
	VoiceAudio->EnvelopeFollowerAttackTime = 20;
	VoiceAudio->EnvelopeFollowerReleaseTime = 100;
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
		// Stop pode disparar OnAudioFinished na hora; não deve puxar a fila
		TGuardValue<bool> StoppingGuard(bStopping, true);
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

bool UPatientVoiceComponent::ReactToText(const FText& DialogueText)
{
	const FString Key = NormalizeDialogue(DialogueText.ToString());
	if (!VoiceLines || Key.IsEmpty())
	{
		return false;
	}

	FName Match = NAME_None;
	VoiceLines->ForeachRow<FPatientVoiceLine>(TEXT("PatientVoice ReactToText"), [&](const FName& RowName, const FPatientVoiceLine& Line)
	{
		if (Match.IsNone() && !Line.Disparo.IsEmpty() && NormalizeDialogue(Line.Disparo) == Key)
		{
			Match = RowName;
		}
	});

	if (Match.IsNone())
	{
		return false;
	}
	if (IsSpeaking())
	{
		PendingLines.Add(Match);
		return true;
	}
	return SpeakLine(Match);
}

bool UPatientVoiceComponent::ReactToQuizWidget(UWidget* QuizWidget)
{
	UWorld* World = QuizWidget ? QuizWidget->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	// Textos visíveis do widget e variáveis de texto do Blueprint (ex.: AnswerText)
	TArray<FText> Texts;
	if (const UTextBlock* TextBlock = Cast<UTextBlock>(QuizWidget))
	{
		Texts.Add(TextBlock->GetText());
	}
	if (const UUserWidget* UserWidget = Cast<UUserWidget>(QuizWidget); UserWidget && UserWidget->WidgetTree)
	{
		UserWidget->WidgetTree->ForEachWidget([&Texts](UWidget* Child)
		{
			if (const UTextBlock* TextBlock = Cast<UTextBlock>(Child))
			{
				Texts.Add(TextBlock->GetText());
			}
		});
	}
	for (TFieldIterator<FProperty> It(QuizWidget->GetClass()); It; ++It)
	{
		if (const FTextProperty* TextProp = CastField<FTextProperty>(*It))
		{
			Texts.Add(TextProp->GetPropertyValue_InContainer(QuizWidget));
		}
		else if (const FStrProperty* StrProp = CastField<FStrProperty>(*It))
		{
			Texts.Add(FText::FromString(StrProp->GetPropertyValue_InContainer(QuizWidget)));
		}
	}

	for (TObjectIterator<UPatientVoiceComponent> Voice; Voice; ++Voice)
	{
		if (Voice->GetWorld() != World || !Voice->HasBegunPlay())
		{
			continue;
		}
		for (const FText& Text : Texts)
		{
			if (Voice->ReactToText(Text))
			{
				return true;
			}
		}
	}
	return false;
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
			const float Scale = 1.0f + FMath::FRandRange(-ShapeVariation, ShapeVariation);
			Cues.Add({ FCString::Atof(*TimeText), ShapeText[0], Scale });
		}
	}
}

void UPatientVoiceComponent::GetShapeWeights(TCHAR Shape, float OutWeights[NumChannels])
{
	// Peso de JawOpen, MouthPucker, MouthWide, LowerLipIn, LipsPress, UpperLipUp e TongueUp para cada
	// forma do Rhubarb: A = M/B/P, B = K/S/T/I, C = E, D = A, E = O, F = U/W, G = F/V, H = L, X = repouso
	static const TMap<TCHAR, TArray<float>> Shapes = {
		{ TEXT('A'), { 0.00f, 0.00f, 0.00f, 0.00f, 0.75f, 0.00f, 0.00f } },
		{ TEXT('B'), { 0.18f, 0.00f, 0.18f, 0.00f, 0.00f, 0.08f, 0.12f } },
		{ TEXT('C'), { 0.50f, 0.00f, 0.18f, 0.00f, 0.00f, 0.06f, 0.00f } },
		{ TEXT('D'), { 0.85f, 0.00f, 0.06f, 0.00f, 0.00f, 0.06f, 0.00f } },
		{ TEXT('E'), { 0.50f, 0.40f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f } },
		{ TEXT('F'), { 0.22f, 0.70f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f } },
		{ TEXT('G'), { 0.14f, 0.00f, 0.06f, 0.70f, 0.00f, 0.10f, 0.00f } },
		{ TEXT('H'), { 0.50f, 0.00f, 0.06f, 0.00f, 0.00f, 0.06f, 0.65f } },
	};

	const TArray<float>* Weights = Shapes.Find(Shape);
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		OutWeights[Channel] = Weights ? (*Weights)[Channel] : 0.0f;
	}
}

void UPatientVoiceComponent::SetShapeTarget(TCHAR Shape)
{
	GetShapeWeights(Shape, TargetMouth);
	TargetMouth[Jaw] *= JawScale;
	for (float& Weight : TargetMouth)
	{
		Weight *= MouthIntensity;
	}
}

void UPatientVoiceComponent::UpdateVisemeTarget()
{
	const float VisemeTime = SpeechTime - VisemeTimeOffset;
	while (CueIndex + 1 < Cues.Num() && Cues[CueIndex + 1].Time <= VisemeTime)
	{
		++CueIndex;
	}

	const FVisemeCue& Cue = Cues[CueIndex];
	float Weights[NumChannels];
	GetShapeWeights(Cue.Shape, Weights);
	for (float& Weight : Weights)
	{
		Weight *= Cue.Scale;
	}

	// Coarticulação: perto da troca, a boca já começa a ir para a forma seguinte
	if (CueIndex + 1 < Cues.Num() && CoarticulationTime > 0.0f)
	{
		const FVisemeCue& Next = Cues[CueIndex + 1];
		const float Remaining = Next.Time - VisemeTime;
		if (Remaining < CoarticulationTime)
		{
			const float Alpha = 0.5f * (1.0f - Remaining / CoarticulationTime);
			float NextWeights[NumChannels];
			GetShapeWeights(Next.Shape, NextWeights);
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				Weights[Channel] = FMath::Lerp(Weights[Channel], NextWeights[Channel] * Next.Scale, Alpha);
			}
		}
	}

	// Sílabas mais fortes abrem mais a boca sem abafar a forma fonética
	const float Loudness = FMath::Clamp(Envelope * EnvelopeGain, 0.0f, 1.0f);
	Weights[Jaw] *= FMath::Lerp(0.85f, 1.25f, Loudness) * JawScale;

	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		TargetMouth[Channel] = FMath::Clamp(Weights[Channel], 0.0f, 1.0f) * MouthIntensity;
	}
}

void UPatientVoiceComponent::HandleEnvelope(const USoundWave* PlayingSoundWave, const float EnvelopeValue)
{
	Envelope = EnvelopeValue;
	if (Cues.Num() == 0 && IsSpeaking())
	{
		TargetMouth[Jaw] = FMath::Clamp(EnvelopeValue * EnvelopeGain * JawScale, 0.0f, 1.0f) * MouthIntensity;
	}
}

void UPatientVoiceComponent::HandleAudioFinished()
{
	// Uma fala nova já começou; o fim da anterior chegou atrasado
	if (bStopping || VoiceAudio->IsPlaying())
	{
		return;
	}

	Cues.Reset();
	SetShapeTarget(TEXT('X'));
	const FName FinishedLine = CurrentLine;
	CurrentLine = NAME_None;
	OnSpeechFinished.Broadcast(FinishedLine);

	if (PendingLines.Num() > 0)
	{
		const FName NextLine = PendingLines[0];
		PendingLines.RemoveAt(0);
		SpeakLine(NextLine);
	}
}

void UPatientVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Cues.Num() > 0 && IsSpeaking())
	{
		SpeechTime += DeltaTime;
		UpdateVisemeTarget();
	}

	bool bMoving = IsSpeaking();
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		const float Speed = Channel == Jaw ? JawSmoothingSpeed : SmoothingSpeed;
		CurrentMouth[Channel] = FMath::FInterpTo(CurrentMouth[Channel], TargetMouth[Channel], DeltaTime, Speed);
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

	const FName Morphs[NumChannels] = {
		JawOpenMorph, MouthPuckerMorph, MouthWideMorph, LowerLipInMorph, LipsPressMorph, UpperLipUpMorph, TongueUpMorph
	};
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		if (!Morphs[Channel].IsNone())
		{
			Mesh->SetMorphTarget(Morphs[Channel], CurrentMouth[Channel]);
		}
	}
}
