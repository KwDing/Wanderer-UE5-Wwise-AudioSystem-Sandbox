#pragma once

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include <atomic>

namespace HeadphoneCapture
{
    // Fixed, test-only storage: no UObject access, allocations or locks on the audio thread.
    inline float Samples[48000 * 2 * 40];
    inline std::atomic<uint32> Count{0};
    inline std::atomic<bool> Overflow{false};

    inline void Copy(AkAudioBuffer& Buffer, AkOutputDeviceID, void*)
    {
        if (Buffer.NumChannels() != 2) { Overflow=true; return; }
        const uint32 Begin = Count.load();
        const uint32 Num = Buffer.uValidFrames * 2;
        if (Begin + Num > UE_ARRAY_COUNT(Samples)) { Overflow=true; return; }
        FMemory::Memcpy(Samples + Begin, Buffer.GetInterleavedData(), Num * sizeof(float));
        Count.store(Begin + Num);
    }

    class FScopedCapture
    {
    public:
        explicit FScopedCapture(IWwiseSoundEngineAPI& InEngine) : Engine(InEngine)
        {
            Count=0;
            Overflow=false;
            bRegistered = Engine.RegisterCaptureCallback(Copy, 0, nullptr) == AK_Success;
        }
        ~FScopedCapture() { Stop(); }
        bool Save(const FString& Path)
        {
            if (!bRegistered) return false;
            Stop();
            if (Overflow.load()) return false;
            TArray<uint8> Bytes;
            Bytes.Append(reinterpret_cast<const uint8*>(Samples), Count.load() * sizeof(float));
            return FFileHelper::SaveArrayToFile(Bytes, *Path);
        }
    private:
        void Stop()
        {
            if (!bRegistered) return;
            Engine.UnregisterCaptureCallback(Copy, 0);
            bRegistered=false;
        }
        IWwiseSoundEngineAPI& Engine;
        bool bRegistered=false;
    };
}
