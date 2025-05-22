// WindowsRepresentationComponent.cpp
#include "WindowsRepresentationComponent.h"
#include "PhysicsEngine/BodySetup.h"

UWindowsRepresentationComponent::UWindowsRepresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    ProceduralMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedWindowsMesh"));
    Thickness = 10.0f;
    bCreateCollision = true;
    NextAvailableSectionIndex = 0;
}

void UWindowsRepresentationComponent::BeginPlay()
{
    Super::BeginPlay();

    if (GetOwner() && GetOwner()->GetRootComponent())
    {
        ProceduralMeshComponent->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

        if (!ProceduralMeshComponent->IsRegistered())
        {
            ProceduralMeshComponent->RegisterComponent();
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("UWindowsRepresentationComponent needs an Owner with a RootComponent to attach its ProceduralMeshComponent."));
    }

    WindowSectionMap.Empty();
    NextAvailableSectionIndex = 0;
    RegenerateMesh();
}

void UWindowsRepresentationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    if (ProceduralMeshComponent && !ProceduralMeshComponent->IsBeingDestroyed())
    {
        ProceduralMeshComponent->ClearAllMeshSections();
        ProceduralMeshComponent->ClearCollisionConvexMeshes();
    }
    WindowSectionMap.Empty();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UWindowsRepresentationComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
    FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

    if (PropertyName == GET_MEMBER_NAME_CHECKED(UWindowsRepresentationComponent, WindowPointSets) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UWindowsRepresentationComponent, Thickness) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UWindowsRepresentationComponent, WindowMaterial) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UWindowsRepresentationComponent, bCreateCollision) ||
        MemberPropertyName == GET_MEMBER_NAME_CHECKED(UWindowsRepresentationComponent, WindowPointSets) ||
        (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetOwnerStruct() == FWindowPoints::StaticStruct())
        )
    {
        RegenerateMesh();
    }
}
#endif

void UWindowsRepresentationComponent::UpdateWindows(const TArray<FWindowPoints>& NewPointSets, float NewThickness)
{
    WindowPointSets = NewPointSets;
    Thickness = NewThickness;
    RegenerateMesh();
}

void UWindowsRepresentationComponent::RegenerateMesh()
{
    if (!ProceduralMeshComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("ProceduralMeshComponent is null in WindowsRepresentationComponent."));
        return;
    }
    if (!GetOwner())
    {
        UE_LOG(LogTemp, Error, TEXT("Owner is null in WindowsRepresentationComponent."));
        return;
    }

    TSet<FName> ActiveWindowNames;
    for (const FWindowPoints& Points : WindowPointSets)
    {
        if (Points.WindowName != NAME_None)
        {
            ActiveWindowNames.Add(Points.WindowName);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("A WindowPointSet has NAME_None for WindowName. It will be ignored. Provide a unique name."));
        }
    }

    TArray<FName> NamesToRemoveFromMap;
    for (auto It = WindowSectionMap.CreateConstIterator(); It; ++It)
    {
        if (!ActiveWindowNames.Contains(It.Key()))
        {
            ProceduralMeshComponent->ClearMeshSection(It.Value());
            NamesToRemoveFromMap.Add(It.Key());
            // UE_LOG(LogTemp, Log, TEXT("Cleared mesh section %d for window '%s' (removed)"), It.Value(), *It.Key().ToString());
        }
    }
    for (const FName& NameToRemove : NamesToRemoveFromMap)
    {
        WindowSectionMap.Remove(NameToRemove);
    }

    if (bCreateCollision)
    {
        ProceduralMeshComponent->ClearCollisionConvexMeshes();
    }
    else
    {
        ProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    TArray<FVector2D> EmptyUVs;

    for (const FWindowPoints& Points : WindowPointSets)
    {
        if (Points.WindowName == NAME_None) continue;

        TArray<FVector> SectionVertices;
        TArray<int32> SectionTriangles;
        TArray<FVector> SectionNormals;
        TArray<FVector2D> SectionUVs0;
        TArray<FLinearColor> SectionVertexColors;
        TArray<FProcMeshTangent> SectionTangents;
        TArray<FVector> ConvexVerticesForCollision;

        AddCuboidFromPoints(Points, Thickness, SectionVertices, SectionTriangles, SectionNormals, SectionUVs0, SectionVertexColors, SectionTangents, ConvexVerticesForCollision);

        if (SectionVertices.Num() == 0 || SectionTriangles.Num() == 0)
        {
            // UE_LOG(LogTemp, Warning, TEXT("Window '%s' resulted in no mesh data."), *Points.WindowName.ToString());
            continue;
        }

        int32 CurrentSectionIndex;
        if (WindowSectionMap.Contains(Points.WindowName))
        {
            CurrentSectionIndex = WindowSectionMap[Points.WindowName];

            ProceduralMeshComponent->UpdateMeshSection_LinearColor(
                CurrentSectionIndex,
                SectionVertices,
                SectionNormals,
                SectionUVs0,
                EmptyUVs,
                EmptyUVs,
                EmptyUVs,
                SectionVertexColors,
                SectionTangents
            );
            // UE_LOG(LogTemp, Log, TEXT("Updated mesh section %d for window '%s'"), CurrentSectionIndex, *Points.WindowName.ToString());
        }
        else
        {
            CurrentSectionIndex = NextAvailableSectionIndex++;
            WindowSectionMap.Add(Points.WindowName, CurrentSectionIndex);

            ProceduralMeshComponent->CreateMeshSection_LinearColor(
                CurrentSectionIndex,
                SectionVertices,
                SectionTriangles,
                SectionNormals,
                SectionUVs0,
                EmptyUVs,           // UV1
                EmptyUVs,           // UV2
                EmptyUVs,           // UV3
                SectionVertexColors,
                SectionTangents,
                false
            );
            // UE_LOG(LogTemp, Log, TEXT("Created mesh section %d for window '%s'"), CurrentSectionIndex, *Points.WindowName.ToString());
        }

        if (WindowMaterial)
        {
            ProceduralMeshComponent->SetMaterial(CurrentSectionIndex, WindowMaterial);
        }

        if (bCreateCollision && ConvexVerticesForCollision.Num() >= 4)
        {
            ProceduralMeshComponent->AddCollisionConvexMesh(ConvexVerticesForCollision);
        }
    }

    if (bCreateCollision)
    {
        if (ProceduralMeshComponent->GetBodySetup())
        {
            ProceduralMeshComponent->GetBodySetup()->CreatePhysicsMeshes();
        }

        ProceduralMeshComponent->SetUseCCD(true);
        ProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    else
    {
        ProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void UWindowsRepresentationComponent::AddCuboidFromPoints(
    const FWindowPoints& Points,
    float CuboidThickness,
    TArray<FVector>& Vertices,
    TArray<int32>& Triangles,
    TArray<FVector>& Normals,
    TArray<FVector2D>& UVs0,
    TArray<FLinearColor>& VertexColors,
    TArray<FProcMeshTangent>& Tangents,
    TArray<FVector>& OutConvexVertices)
{
    Vertices.Empty();
    Triangles.Empty();
    Normals.Empty();
    UVs0.Empty();
    VertexColors.Empty();
    Tangents.Empty();
    OutConvexVertices.Empty();

    const FVector& P1_TL = Points.Point1_TL; // Top-Left
    const FVector& P2_TR = Points.Point2_TR; // Top-Right
    const FVector& P3_BR = Points.Point3_BR; // Bottom-Right
    const FVector& P4_BL = Points.Point4_BL; // Bottom-Left

    FVector SurfaceNormal = FVector::CrossProduct(P2_TR - P1_TL, P4_BL - P1_TL).GetSafeNormal();
    if (SurfaceNormal.IsNearlyZero())
    {
        SurfaceNormal = FVector::CrossProduct(P3_BR - P2_TR, P1_TL - P2_TR).GetSafeNormal();
        if (SurfaceNormal.IsNearlyZero()) {
            UE_LOG(LogTemp, Warning, TEXT("Window points for '%s' are degenerate, cannot calculate normal reliably. Using UpVector as fallback."), *Points.WindowName.ToString());
            SurfaceNormal = FVector::UpVector;
        }
    }

    FVector P1_TL_Back = P1_TL - SurfaceNormal * CuboidThickness;
    FVector P2_TR_Back = P2_TR - SurfaceNormal * CuboidThickness;
    FVector P3_BR_Back = P3_BR - SurfaceNormal * CuboidThickness;
    FVector P4_BL_Back = P4_BL - SurfaceNormal * CuboidThickness;

    FLinearColor DefaultColor = FLinearColor::White;

    OutConvexVertices.Add(P1_TL);
    OutConvexVertices.Add(P2_TR);
    OutConvexVertices.Add(P3_BR);
    OutConvexVertices.Add(P4_BL);
    OutConvexVertices.Add(P1_TL_Back);
    OutConvexVertices.Add(P2_TR_Back);
    OutConvexVertices.Add(P3_BR_Back);
    OutConvexVertices.Add(P4_BL_Back);

    int32 V0 = Vertices.Add(P1_TL); UVs0.Add(FVector2D(0, 0)); Normals.Add(SurfaceNormal); VertexColors.Add(DefaultColor);
    int32 V1 = Vertices.Add(P2_TR); UVs0.Add(FVector2D(1, 0)); Normals.Add(SurfaceNormal); VertexColors.Add(DefaultColor);
    int32 V2 = Vertices.Add(P3_BR); UVs0.Add(FVector2D(1, 1)); Normals.Add(SurfaceNormal); VertexColors.Add(DefaultColor);
    int32 V3 = Vertices.Add(P4_BL); UVs0.Add(FVector2D(0, 1)); Normals.Add(SurfaceNormal); VertexColors.Add(DefaultColor);
    Triangles.Append({ V0, V3, V2, V0, V2, V1 });

    // Surface 1: Back (-SurfaceNormal)
    FVector BackNormal = -SurfaceNormal;
    int32 V4 = Vertices.Add(P1_TL_Back); UVs0.Add(FVector2D(0, 0)); Normals.Add(BackNormal); VertexColors.Add(DefaultColor);
    int32 V5 = Vertices.Add(P4_BL_Back); UVs0.Add(FVector2D(1, 0)); Normals.Add(BackNormal); VertexColors.Add(DefaultColor);
    int32 V6 = Vertices.Add(P3_BR_Back); UVs0.Add(FVector2D(1, 1)); Normals.Add(BackNormal); VertexColors.Add(DefaultColor);
    int32 V7 = Vertices.Add(P2_TR_Back); UVs0.Add(FVector2D(0, 1)); Normals.Add(BackNormal); VertexColors.Add(DefaultColor);
    Triangles.Append({ V4, V7, V6, V4, V6, V5 });

    // Side faces
    auto AddSideFace = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D) {
        FVector SideNormal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
        int32 SV0 = Vertices.Add(A); UVs0.Add(FVector2D(0, 0)); Normals.Add(SideNormal); VertexColors.Add(DefaultColor);
        int32 SV1 = Vertices.Add(B); UVs0.Add(FVector2D(1, 0)); Normals.Add(SideNormal); VertexColors.Add(DefaultColor);
        int32 SV2 = Vertices.Add(C); UVs0.Add(FVector2D(1, 1)); Normals.Add(SideNormal); VertexColors.Add(DefaultColor);
        int32 SV3 = Vertices.Add(D); UVs0.Add(FVector2D(0, 1)); Normals.Add(SideNormal); VertexColors.Add(DefaultColor);
        Triangles.Append({ SV0, SV3, SV2, SV0, SV2, SV1 });
        };

    AddSideFace(P1_TL, P1_TL_Back, P2_TR_Back, P2_TR); // Top Edge P1-P2 (Front P1,P2 -> Back P1b,P2b)
    AddSideFace(P2_TR, P2_TR_Back, P3_BR_Back, P3_BR); // Right Edge P2-P3
    AddSideFace(P3_BR, P3_BR_Back, P4_BL_Back, P4_BL); // Bottom Edge P3-P4
    AddSideFace(P4_BL, P4_BL_Back, P1_TL_Back, P1_TL); // Left Edge P4-P1

    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        const FVector& N = Normals[i];
        const FVector CrossVec = (FMath::Abs(N.Z) < (1.f - KINDA_SMALL_NUMBER)) ? FVector::UpVector : FVector::RightVector;
        FVector TangentX = FVector::CrossProduct(N, CrossVec).GetSafeNormal();
        Tangents.Add(FProcMeshTangent(TangentX, false));
    }
}