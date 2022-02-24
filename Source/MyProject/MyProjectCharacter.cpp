// Copyright Epic Games, Inc. All Rights Reserved.

#include "MyProjectCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "MyProjectPlayerController.h"
#include "E_SkillsType.h"

#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Bomb.h"
#include <Runtime\Engine\Classes\Kismet\KismetSystemLibrary.h>
#include <MyProject\Character_Skill.h>


//////////////////////////////////////////////////////////////////////////
// AMyProjectCharacter

AMyProjectCharacter::AMyProjectCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;
	GetCharacterMovement()->MaxWalkSpeed = 200.f;


	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 700.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm


	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bInheritYaw = false;
	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)


	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("Health component"));

	UCharacter_Skill* FlameComponent = CreateDefaultSubobject<UCharacter_Skill>(TEXT("FlameDensity"));
	FlameComponent->Init(2, 8, ESkillsType::Flame_Size);
	List_Skills.Add(FlameComponent);	

	UCharacter_Skill* BombComponent = CreateDefaultSubobject<UCharacter_Skill>(TEXT("Bomb Number"));
	BombComponent->Init(1, 4, ESkillsType::Bomb_Number);
	List_Skills.Add(BombComponent);

	UCharacter_Skill* SpeedComponent = CreateDefaultSubobject<UCharacter_Skill>(TEXT("Speed"));
	SpeedComponent->Init(1, 6, ESkillsType::Speed);
	List_Skills.Add(SpeedComponent);

}

//////////////////////////////////////////////////////////////////////////
// Input

void AMyProjectCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAction("Bomb", IE_Pressed, this, &AMyProjectCharacter::ThrowBomb);


	PlayerInputComponent->BindAxis("MoveForward", this, &AMyProjectCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AMyProjectCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AMyProjectCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AMyProjectCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &AMyProjectCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &AMyProjectCharacter::TouchStopped);


}


void AMyProjectCharacter::OnSpeedUpdate_Implementation()
{
	GetCharacterMovement()->MaxWalkSpeed = 200.f + 100* List_Skills[ESkillsType::Speed]->value;

}

void AMyProjectCharacter::OnDeath_Implementation()
{
	AMyProjectPlayerController* PlayerController = Cast<AMyProjectPlayerController>(GetController());
	if (PlayerController != nullptr) {
		PlayerController->ShowRestartWidget();
		return;
	}
}


void AMyProjectCharacter::OnTakeDamage_Implementation()
{
	AMyProjectPlayerController* PlayerController = Cast<AMyProjectPlayerController>(GetController());
	if (PlayerController != nullptr) {
		PlayerController->UpdateHUDWidget(HealthComponent->GetHealthPercent());
		return;
	}
}

void AMyProjectCharacter::ThrowBomb()
{
	if (BombClass == nullptr || List_Skills[ESkillsType::Bomb_Number]->value == 0) {
		return;
	}
	
	FVector SpawnLocation = GetActorLocation();
	SpawnLocation.Z -= 60;

	SpawnLocation.X = FloorHundred(SpawnLocation.X);
	SpawnLocation.Y = FloorHundred(SpawnLocation.Y);

	ABomb* Bomb = GetWorld()->SpawnActor<ABomb>(BombClass, SpawnLocation, GetActorRotation());
	Bomb->SetOwner(this);
	Bomb->SetDensity(List_Skills[ESkillsType::Flame_Size]->value);


	if (!HasAuthority()) {
		Server_ThrowBomb(SpawnLocation, GetActorRotation());
	}
	else {
		Multi_ThrowBomb(SpawnLocation, GetActorRotation());
	}
	

}


bool AMyProjectCharacter::Server_ThrowBomb_Validate(FVector Location, FRotator Rotation)
{
	return true;
}

void AMyProjectCharacter::Server_ThrowBomb_Implementation(FVector Location, FRotator Rotation)
{
	Multi_ThrowBomb(Location, Rotation);
}

bool AMyProjectCharacter::Multi_ThrowBomb_Validate(FVector Location, FRotator Rotation)
{
	return true;
}

void AMyProjectCharacter::Multi_ThrowBomb_Implementation(FVector Location, FRotator Rotation){
	if (!IsLocallyControlled()) {
		ABomb* Bomb = GetWorld()->SpawnActor<ABomb>(BombClass, Location, Rotation);
		Bomb->SetOwner(this);
		Bomb->SetDensity(List_Skills[ESkillsType::Flame_Size]->value);
	}
		
}

int AMyProjectCharacter::FloorHundred(float a) {
	int res = 0;

	if (a < 0) {
		res = (a / 100)-0.5;	
	}
	else if(a>0) {
		res = (a / 100) + 0.5;
	}

	return res * 100;

}



void AMyProjectCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
		Jump();
}

void AMyProjectCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
		StopJumping();
}

UCharacter_Skill* AMyProjectCharacter::GetComponentBySkillType(int type)
{
	return List_Skills[type];
}

void AMyProjectCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AMyProjectCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AMyProjectCharacter::MoveForward(float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{

		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void AMyProjectCharacter::MoveRight(float Value)
{
	if ( (Controller != nullptr) && (Value != 0.0f) )
	{

		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}
