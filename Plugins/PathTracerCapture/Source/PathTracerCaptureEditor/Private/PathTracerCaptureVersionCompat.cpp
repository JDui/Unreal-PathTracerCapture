#include "PathTracerCaptureVersionCompat.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Runtime/Launch/Resources/Version.h"

namespace PathTracerCaptureVersionCompat
{
    static FString MakeDateTokenYYMMDD()
    {
        return FDateTime::Now().ToString(TEXT("%y%m%d"));
    }

    static int32 ParseDigitsToInt(const FString& Text, int32 StartIndex)
    {
        if (StartIndex < 0 || StartIndex >= Text.Len())
        {
            return INDEX_NONE;
        }

        int32 Value = 0;
        bool bFoundDigit = false;
        for (int32 Index = StartIndex; Index < Text.Len(); ++Index)
        {
            const TCHAR Ch = Text[Index];
            if (!FChar::IsDigit(Ch))
            {
                break;
            }

            bFoundDigit = true;
            Value = (Value * 10) + static_cast<int32>(Ch - TEXT('0'));
        }

        return bFoundDigit ? Value : INDEX_NONE;
    }

    static int32 ResolveNextSequenceForDate(const FString& OutputDirectory, const FString& DateToken)
    {
        if (OutputDirectory.IsEmpty() || DateToken.IsEmpty())
        {
            return 1;
        }

        TArray<FString> FoundFiles;
        IFileManager::Get().FindFiles(FoundFiles, *(OutputDirectory / TEXT("*.png")), true, false);

        int32 MaxSequence = 0;
        for (const FString& FileName : FoundFiles)
        {
            const FString BaseName = FPaths::GetBaseFilename(FileName);
            int32 SearchFrom = 0;
            while (SearchFrom < BaseName.Len())
            {
                const int32 TokenPos = BaseName.Find(DateToken, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
                if (TokenPos == INDEX_NONE)
                {
                    break;
                }

                const int32 Parsed = ParseDigitsToInt(BaseName, TokenPos + DateToken.Len());
                if (Parsed != INDEX_NONE)
                {
                    MaxSequence = FMath::Max(MaxSequence, Parsed);
                }

                SearchFrom = TokenPos + DateToken.Len();
            }
        }

        return MaxSequence + 1;
    }
}

FString FPathTracerCaptureVersionCompat::BackendToToken(EPathTracerCaptureBackend Backend)
{
    switch (Backend)
    {
    case EPathTracerCaptureBackend::Viewport:
        return TEXT("viewport");
    case EPathTracerCaptureBackend::MovieRenderQueue:
        return TEXT("mrq");
    default:
        return TEXT("unknown");
    }
}

FString FPathTracerCaptureVersionCompat::MakeTimestampToken()
{
    return FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
}

FString FPathTracerCaptureVersionCompat::GetCurrentMapToken()
{
    if (GEditor && GEditor->GetEditorWorldContext().World())
    {
        return FPaths::GetBaseFilename(GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName());
    }
    return TEXT("UnknownMap");
}

FString FPathTracerCaptureVersionCompat::ResolveFilename(const FString& Pattern, const FPathTracerCaptureRequest& Request)
{
    FString Resolved = Pattern.IsEmpty() ? TEXT("AXi_{date}{seq}") : Pattern;
    const FString DateToken = PathTracerCaptureVersionCompat::MakeDateTokenYYMMDD();

    Resolved.ReplaceInline(TEXT("{map}"), *GetCurrentMapToken(), ESearchCase::IgnoreCase);
    Resolved.ReplaceInline(TEXT("{backend}"), *BackendToToken(Request.Backend), ESearchCase::IgnoreCase);
    Resolved.ReplaceInline(TEXT("{spp}"), *FString::FromInt(Request.TargetSPP), ESearchCase::IgnoreCase);
    Resolved.ReplaceInline(TEXT("{timestamp}"), *MakeTimestampToken(), ESearchCase::IgnoreCase);
    Resolved.ReplaceInline(TEXT("{date}"), *DateToken, ESearchCase::IgnoreCase);
    Resolved.ReplaceInline(TEXT("{yymmdd}"), *DateToken, ESearchCase::IgnoreCase);

    if (Resolved.Contains(TEXT("{seq}"), ESearchCase::IgnoreCase))
    {
        const int32 NextSequence = FMath::Max(1, PathTracerCaptureVersionCompat::ResolveNextSequenceForDate(Request.OutputDirectory, DateToken));
        const FString SequenceToken = FString::Printf(TEXT("%03d"), NextSequence);
        Resolved.ReplaceInline(TEXT("{seq}"), *SequenceToken, ESearchCase::IgnoreCase);
    }

    Resolved = FPaths::MakeValidFileName(Resolved);
    if (Resolved.IsEmpty())
    {
        Resolved = TEXT("AXi_PathTracerCapture");
    }
    return Resolved;
}

FIntPoint FPathTracerCaptureVersionCompat::ClampResolution(int32 Width, int32 Height)
{
    return FIntPoint(FMath::Clamp(Width, 16, 16384), FMath::Clamp(Height, 16, 16384));
}

bool FPathTracerCaptureVersionCompat::IsPathTracingSupported(FText& OutReason)
{
    if (!GDynamicRHI)
    {
        OutReason = FText::FromString(TEXT("RHI未初始化。"));
        return false;
    }

    if (!GRHISupportsRayTracing)
    {
        OutReason = FText::FromString(TEXT("当前RHI不支持光线追踪。"));
        return false;
    }

    OutReason = FText::GetEmpty();
    return true;
}
