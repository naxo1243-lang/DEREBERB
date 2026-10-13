#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// Dereverberación por supresión espectral de la cola tardía (modelo de
// decaimiento exponencial, estilo Lebart/Habets). Procesa por STFT con
// ventana raíz-Hann y solapamiento del 75 %.
class DereverbProcessor : public juce::AudioProcessor
{
public:
    DereverbProcessor();
    ~DereverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dereverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int fftOrder = 10;               // 1024 puntos
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;      // 75 % de solape
    static constexpr int numBins  = fftSize / 2 + 1;
    static constexpr int latency  = fftSize - hopSize;

    struct Channel
    {
        std::vector<float> inFifo, outFifo, dry, fftData, gainPrev;
        std::vector<std::vector<float>> history;      // potencias pasadas
        int pos = 0, hopCount = 0, dryPos = 0, histIdx = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float processSample (Channel& c, float x, float mix);
    void processFrame (Channel& c);

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<Channel> channels;
    float norm = 0.5f;
    int lateFrames = 1;
    double sampleRateHz = 44100.0;

    // Parámetros cacheados por bloque
    float amount = 1.0f, lateDecay = 0.5f, floorGain = 0.18f;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* rt60Param   = nullptr;
    std::atomic<float>* floorParam  = nullptr;
    std::atomic<float>* mixParam    = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DereverbProcessor)
};
