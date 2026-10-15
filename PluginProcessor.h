#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// Datos que el audio publica para la interfaz (sin bloqueos).
// El hilo de audio escribe columnas de picos (~150 por segundo) en un anillo;
// la interfaz solo lee.
struct DereverbTelemetry
{
    static constexpr int cols = 512;
    static constexpr double columnsPerSecond = 150.0;

    std::array<std::atomic<float>, cols> inPeak, outPeak;
    std::atomic<int>   head { 0 };          // siguiente columna a escribir
    std::atomic<float> reduction { 0.0f };  // 0..1, fraccion de energia que se esta quitando
    std::atomic<float> inLevel { 0.0f };    // pico suavizado de entrada
    std::atomic<float> outLevel { 0.0f };   // pico suavizado de salida

    void reset()
    {
        for (auto& v : inPeak)  v.store (0.0f);
        for (auto& v : outPeak) v.store (0.0f);
        head.store (0);
        reduction.store (0.0f);
        inLevel.store (0.0f);
        outLevel.store (0.0f);
    }
};

// Dereverberacion por supresion espectral de la cola tardia (modelo de
// decaimiento exponencial, estilo Lebart/Habets). Procesa por STFT con
// ventana raiz-Hann y solapamiento del 75 %.
class DereverbProcessor : public juce::AudioProcessor
{
public:
    DereverbProcessor();
    ~DereverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dereverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    DereverbTelemetry telemetry;

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
        float lastReduction = 0.0f;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void process (juce::AudioBuffer<float>&, bool hostBypassed);
    float processSample (Channel& c, float x, float mix, float bypass, bool delta);
    void processFrame (Channel& c);

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<Channel> channels;
    float norm = 0.5f;
    int lateFrames = 1;
    double sampleRateHz = 44100.0;

    // Parametros cacheados por bloque
    float amount = 1.0f, lateDecay = 0.5f, floorGain = 0.18f;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* rt60Param   = nullptr;
    std::atomic<float>* floorParam  = nullptr;
    std::atomic<float>* mixParam    = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* deltaParam  = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;

    // Suavizado de parametros y datos para el osciloscopio
    juce::SmoothedValue<float> mixSmooth, bypassSmooth;
    int samplesPerColumn = 320, columnCounter = 0;
    float columnIn = 0.0f, columnOut = 0.0f;
    float reductionSmooth = 0.0f, inLevelSmooth = 0.0f, outLevelSmooth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DereverbProcessor)
};
