// WindowsRepresentationComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "WindowsRepresentationComponent.generated.h"

USTRUCT(BlueprintType)
struct FWindowPoints
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Data")
    FName WindowName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Data", Meta = (MakeEditWidget = true))
    FVector Point1_TL;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Data", Meta = (MakeEditWidget = true))
    FVector Point2_TR;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Data", Meta = (MakeEditWidget = true))
    FVector Point3_BR;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Data", Meta = (MakeEditWidget = true))
    FVector Point4_BL;

    FWindowPoints()
    {
        WindowName = NAME_None;
        Point1_TL = FVector::ZeroVector;
        Point2_TR = FVector(100.f, 0.f, 0.f);
        Point3_BR = FVector(100.f, 100.f, 0.f);
        Point4_BL = FVector(0.f, 100.f, 0.f);
    }
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WINDOWTRANSPARENCY_API UWindowsRepresentationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UWindowsRepresentationComponent();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UProceduralMeshComponent> ProceduralMeshComponent; // UE5 スタイル: TObjectPtr

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Settings", Meta = (TitleProperty = "WindowName")) // TitlePropertyをWindowNameに
        TArray<FWindowPoints> WindowPointSets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Settings", meta = (ClampMin = "0.1"))
    float Thickness;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Settings")
    UMaterialInterface* WindowMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window Settings")
    bool bCreateCollision;

    UFUNCTION(BlueprintCallable, Category = "Procedural Window")
    void UpdateWindows(const TArray<FWindowPoints>& NewPointSets, float NewThickness);

    UFUNCTION(BlueprintCallable, Category = "Procedural Window")
    void RegenerateMesh();

protected:
    virtual void BeginPlay() override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    void AddCuboidFromPoints(
        const FWindowPoints& Points,
        float CuboidThickness,
        TArray<FVector>& Vertices,
        TArray<int32>& Triangles,
        TArray<FVector>& Normals,
        TArray<FVector2D>& UVs0,
        TArray<FLinearColor>& VertexColors,
        TArray<FProcMeshTangent>& Tangents,
        TArray<FVector>& OutConvexVertices
    );

    TMap<FName, int32> WindowSectionMap;
    int32 NextAvailableSectionIndex;
};