// 


#include "SsLightFlicker.h"

#include "Net/UnrealNetwork.h"

TMap<ELightingCurveType, FRichCurve> USsLightFlickerHelper::Curves;
TMap<FString, FRichCurve> USsLightFlickerHelper::CustomCurves;
FCriticalSection USsLightFlickerHelper::CriticalSection;

// Quake lighting flicker functions
// https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/qw-qc/world.qc#L328-L372
const TMap<ELightingCurveType, FString> USsLightFlickerHelper::QuakeCurveSources {
	{ ELightingCurveType::Flicker1, TEXT("mmnmmommommnonmmonqnmmo") },
	{ ELightingCurveType::SlowStrongPulse, TEXT("abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba") },
	{ ELightingCurveType::Candle1, TEXT("mmmmmaaaaammmmmaaaaaabcdefgabcdefg") },
	{ ELightingCurveType::FastStrobe, TEXT("mamamamamama") },
	{ ELightingCurveType::GentlePulse1, TEXT("jklmnopqrstuvwxyzyxwvutsrqponmlkj") },
	{ ELightingCurveType::Flicker2, TEXT("nmonqnmomnmomomno") },
	{ ELightingCurveType::Candle2, TEXT("mmmaaaabcdefgmmmmaaaammmaamm") },
	{ ELightingCurveType::Candle3, TEXT("mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa") },
	{ ELightingCurveType::SlowStrobe, TEXT("aaaaaaaazzzzzzzz") },
	{ ELightingCurveType::FlourescentFlicker, TEXT("mmamammmmammamamaaamammma") },
	{ ELightingCurveType::SlowPulseNoBlack, TEXT("abcdefghijklmnopqrrqponmlkjihgfedcba") },
};

float USsLightFlickerHelper::EvaluateLightCurve(ELightingCurveType CurveType, float Time)
{
	return GetLightCurve(CurveType).Eval(Time);
}

const FRichCurve& USsLightFlickerHelper::GetLightCurve(ELightingCurveType CurveType)
{
	FScopeLock ScopeLock(&CriticalSection);

	if (auto pCurve = Curves.Find(CurveType))
	{
		return *pCurve;
	}

	auto& Curve = Curves.Emplace(CurveType);
	BuildCurve(CurveType, Curve);
	return Curve;
}

const FRichCurve& USsLightFlickerHelper::GetLightCurve(const FString& CurveStr)
{
	FScopeLock ScopeLock(&CriticalSection);

	if (auto pCurve = CustomCurves.Find(CurveStr))
	{
		return *pCurve;
	}

	auto& Curve = CustomCurves.Emplace(CurveStr);
	BuildCurve(CurveStr, Curve);
	return Curve;
}

void USsLightFlickerHelper::BuildCurve(ELightingCurveType CurveType, FRichCurve& OutCurve)
{
	if (auto pTxt = QuakeCurveSources.Find(CurveType))
	{
		BuildCurve(*pTxt, OutCurve);
	}
	
}

void USsLightFlickerHelper::BuildCurve(const FString& QuakeCurveChars, FRichCurve& OutCurve)
{
	OutCurve.Reset();

	for (int i = 0; i < QuakeCurveChars.Len(); ++i)
	{
		// We actually build the curve a..z = 0..1, and then use a default max value of 2 to restore the original behaviour.
		// Actually the curve is 0..1.04 due to original behaviour that z is 2.08 not 2
		const int CharIndex = QuakeCurveChars[i] - 'a';
		const float Val = (float)CharIndex / 24.f; // to ensure m==1, z==2.08 (rescaled to half that so 0..1.04)
		// Quake default was each character was 0.1s
		OutCurve.AddKey(i * 0.1f, Val);
	}

	// To catch empty
	if (QuakeCurveChars.IsEmpty())
	{
		OutCurve.AddKey(0, 1);
	}
}

USsLightFlickerComponent::USsLightFlickerComponent(const FObjectInitializer& Initializer):
	Super(Initializer),
	TimePos(0),
	CurrentValue(0),
	Curve(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USsLightFlickerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (CurveType == ELightingCurveType::Custom)
	{
		Curve = &USsLightFlickerHelper::GetLightCurve(CustomLightCurveString);
	}
	else
	{
		Curve = &USsLightFlickerHelper::GetLightCurve(CurveType);
	}
	TimePos = 0;
	if (bAutoPlay)
	{
		Play();
	}
}

void USsLightFlickerComponent::ValueUpdate()
{
	CurrentValue = FMath::Lerp(MinValue, MaxValue, Curve->Eval(TimePos));
	OnLightCurveUpdated.Broadcast(CurrentValue);
}

void USsLightFlickerComponent::Play(bool bResetTime)
{
	if (GetOwnerRole() == ROLE_Authority || !GetIsReplicated())
	{
		if (bResetTime)
		{
			TimePos = 0;
		}
		ValueUpdate();

		PrimaryComponentTick.SetTickFunctionEnable(true);
	}
}

void USsLightFlickerComponent::Pause()
{
	if (GetOwnerRole() == ROLE_Authority || !GetIsReplicated())
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
	}
}

float USsLightFlickerComponent::GetCurrentValue() const
{
	return CurrentValue;
}

void USsLightFlickerComponent::OnRep_TimePos()
{
	ValueUpdate();
}

void USsLightFlickerComponent::TickComponent(float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TimePos += DeltaTime * Speed;
	const float MaxTime = Curve->GetLastKey().Time;
	while (TimePos > MaxTime)
	{
		TimePos -= MaxTime;
	}
	ValueUpdate();
}

void USsLightFlickerComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(USsLightFlickerComponent, TimePos);
}
