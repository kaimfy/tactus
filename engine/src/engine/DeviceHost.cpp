#include "tactus/engine/DeviceHost.h"

namespace tactus
{

DeviceHost::DeviceHost (EngineController& c) : controller (c) {}

DeviceHost::~DeviceHost()
{
    close();
}

juce::String DeviceHost::open (const juce::XmlElement* savedState)
{
    // No inputs yet (recording arrives in Phase 2); stereo out.
    const auto error = manager.initialise (0, 2, savedState, true);
    manager.addAudioCallback (this);
    if (error.isEmpty() && manager.getCurrentAudioDevice() == nullptr)
        return "No audio output device is available.";
    return error;
}

juce::String DeviceHost::setInputEnabled (bool enabled)
{
    if (enabled == inputsWanted)
        return {};

    if (! enabled)
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        manager.getAudioDeviceSetup (setup);
        setup.inputDeviceName.clear();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        const auto error = manager.setAudioDeviceSetup (setup, true);
        inputsWanted = false;
        return error;
    }

    // The manager was opened asking for no inputs; reopen it asking for two, keeping the
    // user's output choice.
    const auto state = manager.createStateXml();
    manager.closeAudioDevice();
    auto error = manager.initialise (2, 2, state.get(), true);
    if (error.isNotEmpty())
        return error;

    if (numActiveInputs() == 0)
    {
        // No input was chosen yet: take the system default for the current driver type.
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        manager.getAudioDeviceSetup (setup);
        if (auto* type = manager.getCurrentDeviceTypeObject())
        {
            const auto names = type->getDeviceNames (true);
            if (names.isEmpty())
                return "No audio input device (microphone or line in) was found.";
            setup.inputDeviceName = names[juce::jlimit (0, names.size() - 1, type->getDefaultDeviceIndex (true))];
        }
        setup.useDefaultInputChannels = true;
        error = manager.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty())
            return error;
    }

    if (numActiveInputs() == 0)
        return "The audio input could not be opened. Check the input device in Options > Audio settings, "
               "and that this application is allowed to use the microphone.";

    inputsWanted = true;
    return {};
}

int DeviceHost::numActiveInputs() const
{
    if (auto* device = manager.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

double DeviceHost::sampleRate() const
{
    if (auto* device = manager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return 0.0;
}

int DeviceHost::roundTripLatencySamples() const
{
    if (auto* device = manager.getCurrentAudioDevice())
        return juce::jmax (0, device->getInputLatencyInSamples()) + juce::jmax (0, device->getOutputLatencyInSamples());
    return 0;
}

void DeviceHost::close()
{
    manager.removeAudioCallback (this);
    manager.closeAudioDevice();
}

juce::String DeviceHost::takeLastError()
{
    const juce::ScopedLock lock (errorLock);
    return std::exchange (lastError, {});
}

juce::String DeviceHost::describe() const
{
    if (auto* device = manager.getCurrentAudioDevice())
    {
        const double rate = device->getCurrentSampleRate();
        const int buffer = device->getCurrentBufferSizeSamples();
        const double latencyMs = rate > 0.0 ? 1000.0 * (buffer + device->getOutputLatencyInSamples()) / rate : 0.0;
        return device->getTypeName() + ": " + device->getName() + "  |  "
               + juce::String (rate / 1000.0, 1) + " kHz  |  " + juce::String (buffer) + " smp  |  "
               + juce::String (latencyMs, 1) + " ms";
    }
    return "No audio device";
}

void DeviceHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called by JUCE before the first callback, with the callback not running.
    const int blockSize = device->getCurrentBufferSizeSamples();
    spareChannel.assign ((std::size_t) juce::jmax (64, blockSize * 4), 0.0f);
    controller.prepare (device->getCurrentSampleRate(), blockSize);
    running = true;
}

void DeviceHost::audioDeviceStopped()
{
    running = false;
}

void DeviceHost::audioDeviceError (const juce::String& errorMessage)
{
    const juce::ScopedLock lock (errorLock);
    lastError = errorMessage;
}

void DeviceHost::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                   float* const* out, int numOut,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;

    if (numOut <= 0)
        return;

    auto& engine = controller.engine();
    const int spare = (int) spareChannel.size();

    if (numOut >= 2)
    {
        engine.process (out[0], out[1], numSamples);
        // Capture (and optionally monitor) the input against the block that was just rendered.
        audioRecorder.process (in, numIn, numSamples, engine.lastBlockWasPlaying(), engine.lastBlockStartTick(),
                               out[0], out[1]);
    }
    else
    {
        // Mono device: render stereo in pieces that fit the spare buffer and fold down.
        for (int done = 0; done < numSamples;)
        {
            const int n = juce::jmin (spare, numSamples - done);
            engine.process (out[0] + done, spareChannel.data(), n);
            for (int i = 0; i < n; ++i)
                out[0][done + i] = 0.5f * (out[0][done + i] + spareChannel[(std::size_t) i]);
            done += n;
        }
        // Mono output: record, but there is no stereo bus to monitor into.
        audioRecorder.process (in, numIn, numSamples, engine.lastBlockWasPlaying(), engine.lastBlockStartTick(),
                               nullptr, nullptr);
    }

    for (int ch = 2; ch < numOut; ++ch)
        juce::FloatVectorOperations::clear (out[ch], numSamples);
}

} // namespace tactus
