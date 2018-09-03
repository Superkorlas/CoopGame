// Fill out your copyright notice in the Description page of Project Settings.

#include "STrackerBot.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/Character.h"
#include "SHealthComponent.h"
#include "Components/SphereComponent.h"
#include "SCharacter.h"
#include "Sound/SoundCue.h"
#include "DrawDebugHelpers.h"

static int32 DebugTrackerBotDrawing = 0;
FAutoConsoleVariableRef CVARDebugTrackerBotDrawing(
	TEXT("COOP.DebugTrackerBot"),
	DebugTrackerBotDrawing,
	TEXT("Draw Debug Lines for TrackerBot"),
	ECVF_Cheat);

// Sets default values
ASTrackerBot::ASTrackerBot()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetCanEverAffectNavigation(false);
	MeshComp->SetSimulatePhysics(true);
	RootComponent = MeshComp;

	HealthComp = CreateDefaultSubobject<USHealthComponent>(TEXT("HealthComp"));
	HealthComp->OnHealthChanged.AddDynamic(this, &ASTrackerBot::HandleTakeDamage);

	SphereComp = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComp"));
	SphereComp->SetSphereRadius(200.0f);
	SphereComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SphereComp->SetupAttachment(RootComponent);

	bUseVelocityChange = true;
	MovementForce = 500.0f;
	RequireDistanceToTarget = 300.0f;

	ExplosionDamage = 60.0f;
	ExplosionRadius = 350.0f;

	bStartedSelfDestruction = false;
	SelfDamageInterval = 0.25f;
}

// Called when the game starts or when spawned
void ASTrackerBot::BeginPlay()
{
	Super::BeginPlay();

	if (Role == ROLE_Authority) {
		// Find initial move-to
		NextPathPoint = GetNextPathPoint();

		// Every second we update our power-level based on nearby bots (challenge code)
		FTimerHandle TimerHandle_CheckPowerLevel;
		GetWorldTimerManager().SetTimer(TimerHandle_CheckPowerLevel, this, &ASTrackerBot::OnCheckNearbyBots, 1.0f, true);
	}
}

void ASTrackerBot::HandleTakeDamage(USHealthComponent* OwningHealthComp, float Health, float HealthDelta, const UDamageType * DamageType, AController * InstigatedBy, AActor * DamageCauser)
{
	if (MatInst == nullptr) {
		MatInst = MeshComp->CreateAndSetMaterialInstanceDynamicFromMaterial(0, MeshComp->GetMaterial(0));
	}
	if (MatInst) {
		MatInst->SetScalarParameterValue("LastTimeDamageTaken", GetWorld()->TimeSeconds);
	}

	// Explode on HitPoints == 0
	if (Health <= 0.0f) {
		SelfDestruct();
	}
}

FVector ASTrackerBot::GetNextPathPoint()
{    
	AActor* BestTarget = nullptr;
	float NearestTargetDistance = FLT_MAX;

	for (FConstPawnIterator It = GetWorld()->GetPawnIterator(); It; ++It)
	{
		APawn* TestPawn = It->Get();
		if (TestPawn == nullptr || USHealthComponent::IsFriendly(TestPawn, this))
		{
			continue;
		}

		USHealthComponent* TestPawnHealthComp = Cast<USHealthComponent>(TestPawn->GetComponentByClass(USHealthComponent::StaticClass()));
		if (TestPawnHealthComp && TestPawnHealthComp->GetHealth() > 0.0f)
		{
			float Distance = (TestPawn->GetActorLocation() - GetActorLocation()).Size();
			if (Distance < NearestTargetDistance)
			{
				BestTarget = TestPawn;
				NearestTargetDistance = Distance;
			}
		}
	}

	if (BestTarget)
	{
		UNavigationPath* NavPath = UNavigationSystemV1::FindPathToActorSynchronously(this, GetActorLocation(), BestTarget);

		GetWorldTimerManager().ClearTimer(TimerHandle_RefreshPath);
		GetWorldTimerManager().SetTimer(TimerHandle_RefreshPath, this, &ASTrackerBot::RefreshPath, 5.0, false);

		if (NavPath != nullptr) {
			if (NavPath->PathPoints.Num() > 1) {
				// Return next point in the path
				return NavPath->PathPoints[1];
			}
		}
	}
	
	// Fail to find path
	return GetActorLocation();
}

void ASTrackerBot::SelfDestruct()
{
	if (bExploded) {
		return;
	}

	bExploded = true; 
	
	UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ExplosionEffect, GetActorLocation());
	UGameplayStatics::PlaySoundAtLocation(this, ExplodeSound, GetActorLocation());

	MeshComp->SetVisibility(false, true);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (Role == ROLE_Authority) {
		TArray<AActor*> IgnoredActors;
		IgnoredActors.Add(this);

		// Increase the damage based on the power level (challenge code)
		float ActualDamage = ExplosionDamage + (ExplosionDamage * PowerLevel);

		// Apply damage!
		UGameplayStatics::ApplyRadialDamage(this, ActualDamage, GetActorLocation(), ExplosionRadius, nullptr, IgnoredActors, this, GetInstigatorController(), true);
		
		if (DebugTrackerBotDrawing)
		{
			DrawDebugSphere(GetWorld(), GetActorLocation(), ExplosionRadius, 12, FColor::Red, false, 2.0f, 0, 1.0f);
		}

		SetLifeSpan(2.0f);
	}
}

// Called every frame
void ASTrackerBot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Role == ROLE_Authority && !bExploded) {
		float DistanceToTarget = (GetActorLocation() - NextPathPoint).Size();
		if (DistanceToTarget <= RequireDistanceToTarget) {
			NextPathPoint = GetNextPathPoint();
			if (DebugTrackerBotDrawing)
			{
				DrawDebugString(GetWorld(), GetActorLocation(), "Target Reached!");
			}
		} else {
			// Keep moving towards next target
			FVector ForceDirection = NextPathPoint - GetActorLocation();
			ForceDirection.Normalize();
			ForceDirection *= MovementForce;
			MeshComp->AddForce(ForceDirection, NAME_None, bUseVelocityChange);

			if (DebugTrackerBotDrawing)
			{
				DrawDebugDirectionalArrow(GetWorld(), GetActorLocation(), GetActorLocation() + ForceDirection, 32, FColor::Yellow, false, 0.0f, 0, 1.0f);
			}
		}

		if (DebugTrackerBotDrawing)
		{
			DrawDebugSphere(GetWorld(), NextPathPoint, 20, 12, FColor::Yellow, false, 0.0f, 1.0f);
		}
	}
}

void ASTrackerBot::DamageSelf()
{
	UGameplayStatics::ApplyDamage(this, 20.0f, GetInstigatorController(), this, nullptr);
}

void ASTrackerBot::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	if (!bStartedSelfDestruction && !bExploded) {
		ASCharacter* PlayerPawn = Cast<ASCharacter>(OtherActor);
		if (PlayerPawn && !USHealthComponent::IsFriendly(OtherActor, this)) {
			// We overlapped with a player
			
			if (Role == ROLE_Authority) {
				// Start self destruction sequence
				GetWorldTimerManager().SetTimer(TimerHandle_SelfDamage, this, &ASTrackerBot::DamageSelf, SelfDamageInterval, true, 0.0f);
			}
			bStartedSelfDestruction = true;

			UGameplayStatics::SpawnSoundAttached(SelfDestructSound, RootComponent);
		}
	}
}

// Challenge code (trackerbot)
void ASTrackerBot::OnCheckNearbyBots()
{
	// Distance to check for nearby bots
	const float Radius = 600.0f;

	// Create temporary collision shape for overlaps
	FCollisionShape CollShape;
	CollShape.SetSphere(Radius);

	// Only find pawns (eg. players and AI bots)
	FCollisionObjectQueryParams QueryParams;
	// Our tracker bot's mesh component is set to Physics Body in Blueprint (default profile)
	QueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	QueryParams.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, QueryParams, CollShape);

	if (DebugTrackerBotDrawing)
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), Radius, 12, FColor::White, false, 1.0f);
	}

	int32 NrOfBots = 0;
	// Loop over the results using a "range based for loop"
	for (FOverlapResult Result : Overlaps) {
		// Check if we overlapped with another tracker bot
		ASTrackerBot* Bot = Cast<ASTrackerBot>(Result.GetActor());
		if (Bot && Bot != this) {
			NrOfBots++;
		}
	}

	const int32 MaxPowerLevel = 4;

	// Clamp between min=0 and max=4
	PowerLevel = FMath::Clamp(NrOfBots, 0, MaxPowerLevel);

	// Update the material color
	if (MatInst == nullptr) {
		MatInst = MeshComp->CreateAndSetMaterialInstanceDynamicFromMaterial(0, MeshComp->GetMaterial(0));
	}

	if (MatInst) {
		float Alpha = PowerLevel / (float)MaxPowerLevel;

		MatInst->SetScalarParameterValue("PowerLevelAlpha", Alpha);
	}

	if (DebugTrackerBotDrawing)
	{
		DrawDebugString(GetWorld(), GetActorLocation(), FString::FromInt(NrOfBots), this, FColor::White, 1.0f, true);
	}
}

void ASTrackerBot::RefreshPath()
{
	NextPathPoint = GetNextPathPoint();
}
