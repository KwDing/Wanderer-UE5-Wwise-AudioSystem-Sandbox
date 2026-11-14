#include "UI/Mapping/MapArtworkUtils.h"

namespace MapArtworkUtils
{
    const FLinearColor Ink(0.105f, 0.078f, 0.047f);
    const FLinearColor Paper(0.64f, 0.55f, 0.39f);
    const FLinearColor Road(0.65f, 0.56f, 0.40f);

    FLinearColor MapSurfaceColor(FName InSurface)
    {
        if (InSurface == TEXT("grass"))
        {
            return FLinearColor(0.31f, 0.34f, 0.22f);
        }
        if (InSurface == TEXT("snow"))
        {
            return FLinearColor(0.81f, 0.79f, 0.68f);
        }
        if (InSurface == TEXT("sand"))
        {
            return FLinearColor(0.61f, 0.47f, 0.28f);
        }
        if (InSurface == TEXT("soil"))
        {
            return FLinearColor(0.56f, 0.46f, 0.32f);
        }
        if (InSurface == TEXT("water"))
        {
            return FLinearColor(0.27f, 0.36f, 0.36f);
        }
        if (InSurface == TEXT("building"))
        {
            return FLinearColor(0.32f, 0.25f, 0.20f);
        }
        if (InSurface == TEXT("tunnel"))
        {
            return FLinearColor(0.23f, 0.24f, 0.22f);
        }
        return Paper;
    }

    void DrawRegionInk(const FGeometry& InGeometry,
        FSlateWindowElementList& InOutElements,
        int32 InLayer,
        const FSlateRect& InBounds,
        FName InSurface)
    {
        const bool bStructure = InSurface == TEXT("building") || InSurface == TEXT("tunnel");
        const bool bGrass = InSurface == TEXT("grass");
        const bool bWater = InSurface == TEXT("water");
        const bool bSnow = InSurface == TEXT("snow");
        const double Spacing = bStructure ? 4.5 : bWater ? 10.0 : 6.0;
        // Stable irregular pencil marks. Each centre belongs to one survey run, while
        // short strokes can soften its edge without exposing the raster rectangles.
        for (double X = FMath::FloorToDouble((InBounds.Left - 2.0) / Spacing) * Spacing;
            X < InBounds.Right + 2.0;
            X += Spacing)
        {
            for (double Y = FMath::FloorToDouble((InBounds.Top - 2.0) / Spacing) * Spacing;
                Y < InBounds.Bottom + 2.0;
                Y += Spacing)
            {
                const uint32 Seed = uint32(FMath::RoundToInt(X * 31.0)) * 73856093u
                                    ^ uint32(FMath::RoundToInt(Y * 29.0)) * 19349663u;
                const FVector2D Point(X + (double(Seed % 101) / 100.0 - 0.5) * 3.0,
                    Y + (double((Seed >> 8) % 101) / 100.0 - 0.5) * 3.0);
                if (Point.X < InBounds.Left || Point.X >= InBounds.Right || Point.Y < InBounds.Top
                    || Point.Y >= InBounds.Bottom)
                {
                    continue;
                }
                if (!bStructure && Seed % (bGrass ? 5 : 3) == 0)
                {
                    continue;
                }
                const float Alpha = (bStructure  ? 0.46f
                                        : bGrass ? 0.35f
                                                 : 0.18f)
                                    * (0.65f + float(Seed % 7) * 0.05f);
                const FLinearColor Pencil(Ink.R, Ink.G, Ink.B, Alpha);
                TArray<FVector2D> Stroke;
                if (bGrass)
                {
                    Stroke = {Point + FVector2D(-2.2, -1.5),
                        Point + FVector2D(0, 1.5),
                        Point + FVector2D(1.5, -2.2)};
                }
                else if (bWater || bSnow)
                {
                    Stroke = {Point, Point + FVector2D(0.65, -0.25)};
                }
                else
                {
                    Stroke = {Point + FVector2D(-2.0, 1.6), Point + FVector2D(2.0, -1.4)};
                }
                FSlateDrawElement::MakeLines(InOutElements,
                    InLayer,
                    InGeometry.ToPaintGeometry(),
                    Stroke,
                    ESlateDrawEffect::None,
                    Pencil,
                    true,
                    0.7f);
            }
        }
    }

} // namespace MapArtworkUtils
