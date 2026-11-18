#pragma once

namespace UE::Wanderer::MapScan
{
    constexpr double FrameBudgetSeconds = 0.002;
    constexpr double CapsuleClearance = 2.0;
    constexpr double EntranceMergeDistance = 100.0;
    constexpr double EntranceMergeDistanceSquared = EntranceMergeDistance * EntranceMergeDistance;
    constexpr double BackdropExtent = 40000.0;
    constexpr double VerticalTracePadding = 500.0;
    constexpr double MinimumSurveyCellSize = 60.0;
    constexpr double TargetSurveyCells = 50000.0;
    constexpr double WaterHeightTolerance = 4.0;
    constexpr double LakeContourLimit = 0.99;
    constexpr double RearPoolNarrowWidth = 505.0;
    constexpr double RearPoolWideWidth = 620.0;
    constexpr double PoolEdgeInset = 2.0;
    constexpr double PoolContourLimit = 0.98;
    constexpr double RegionMergeTolerance = 0.1;
} // namespace UE::Wanderer::MapScan
