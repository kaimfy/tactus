#pragma once

#include "tactus/engine/EngineController.h"
#include "tactus/engine/Recorder.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <vector>

namespace tactus
{

/** Connects the engine to a sound card through JUCE's device layer
    (WASAPI / DirectSound on Windows, CoreAudio on macOS, ALSA / JACK on Linux). */
class DeviceHost final : private juce::AudioIODeviceCallback
{
public:
    explicit DeviceHost (EngineController&);
    ~DeviceHost() override;

    /** Opens the saved device setup, or the system default. Returns an error message, or
        an empty string on success. */
    juce::String open (const juce::XmlElement* savedState);
    void close();

    juce::AudioDeviceManager& deviceManager() noexcept { return manager; }

    /** Audio inputs are opened only on request (the first time the user records), so the
        application does not hold the microphone open just by running.
        Returns an error message, or empty on success. */
    juce::String setInputEnabled (bool enabled);
    bool isInputEnabled() const noexcept { return inputsWanted; }
    /** Number of input channels actually open (0 if none). */
    int numActiveInputs() const;
    double sampleRate() const;
    /** Input + output latency reported by the driver, in samples: how late a performance
        played along to the playback arrives in the recording. */
    int roundTripLatencySamples() const;

    Recorder& recorder() noexcept { return audioRecorder; }

    bool isRunning() const noexcept { return running.load(); }
    double cpuLoad() const { return manager.getCpuUsage(); }
    int xrunCount() const noexcept { return manager.getXRunCount(); }
    /** Last error reported by the driver while running, cleared by reading. */
    juce::String takeLastError();
    /** One-line description of the open device, for the status bar and the log. */
    juce::String describe() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

    EngineController& controller;
    juce::AudioDeviceManager manager;
    Recorder audioRecorder;
    bool inputsWanted = false;
    std::vector<float> spareChannel; // right channel when the device is mono
    std::atomic<bool> running { false };
    juce::CriticalSection errorLock; // never taken on the audio thread
    juce::String lastError;
};

} // namespace tactus
