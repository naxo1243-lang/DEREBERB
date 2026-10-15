#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float lateStartSeconds = 0.05f;   // la "cola tardia" empieza a los 50 ms
}

DereverbProcessor::DereverbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    amountParam = apvts.getRawParameterValue ("amount");
    rt60Param   = apvts.getRawParameterValue ("rt60");
    floorParam  = apvts.getRawParameterValue ("floor");
    mixParam    = apvts.getRawParameterValue ("mix");
    bypassParam = apvts.getRawParameterValue ("bypass");
    deltaParam  = apvts.getRawParameterValue ("delta");
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));

    telemetry.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout DereverbProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<P> (juce::ParameterID { "amount", 1 }, "Cantidad",
                                     R (0.0f, 3.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<P> (juce::ParameterID { "rt60", 1 }, "RT60 sala (s)",
                                     R (0.2f, 3.0f, 0.01f, 0.6f), 0.8f));
    layout.add (std::make_unique<P> (juce::ParameterID { "floor", 1 }, "Reduccion max (dB)",
                                     R (-40.0f, -3.0f, 0.1f), -15.0f));
    layout.add (std::make_unique<P> (juce::ParameterID { "mix", 1 }, "Mezcla",
                                     R (0.0f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delta", 1 }, "Delta (escuchar lo que se quita)", false));
    return layout;
}

bool DereverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void DereverbProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    // Ventana raiz-Hann periodica (analisis y sintesis)
    window.assign ((size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        const float hann = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));
        window[(size_t) i] = std::sqrt (hann);
    }

    // Normalizacion: suma de w^2 de los 4 frames solapados
    float s = 0.0f;
    for (int j = 0; j < fftSize / hopSize; ++j)
        s += window[(size_t) (j * hopSize)] * window[(size_t) (j * hopSize)];
    norm = 1.0f / s;

    lateFrames = juce::jmax (1, (int) std::lround (lateStartSeconds * sampleRate / hopSize));

    channels.assign ((size_t) juce::jmax (1, getTotalNumInputChannels()), Channel {});
    for (auto& c : channels)
    {
        c.inFifo.assign ((size_t) fftSize, 0.0f);
        c.outFifo.assign ((size_t) fftSize, 0.0f);
        c.dry.assign ((size_t) fftSize, 0.0f);
        c.fftData.assign ((size_t) fftSize * 2, 0.0f);
        c.gainPrev.assign ((size_t) numBins, 1.0f);
        c.history.assign ((size_t) lateFrames, std::vector<float> ((size_t) numBins, 0.0f));
        c.pos = c.hopCount = c.dryPos = c.histIdx = 0;
        c.lastReduction = 0.0f;
    }

    mixSmooth.reset (sampleRate, 0.02);
    mixSmooth.setCurrentAndTargetValue (mixParam->load());
    bypassSmooth.reset (sampleRate, 0.02);
    bypassSmooth.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);

    samplesPerColumn = juce::jmax (1, (int) (sampleRate / DereverbTelemetry::columnsPerSecond));
    columnCounter = 0;
    columnIn = columnOut = 0.0f;
    reductionSmooth = inLevelSmooth = outLevelSmooth = 0.0f;
    telemetry.reset();

    setLatencySamples (latency);
}

void DereverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void DereverbProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

void DereverbProcessor::process (juce::AudioBuffer<float>& buffer, bool hostBypassed)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Parametros -> coeficientes
    amount    = amountParam->load();
    floorGain = juce::Decibels::decibelsToGain (floorParam->load());
    const float rt60 = rt60Param->load();
    const bool delta = deltaParam->load() > 0.5f;
    mixSmooth.setTargetValue (mixParam->load());
    bypassSmooth.setTargetValue ((hostBypassed || bypassParam->load() > 0.5f) ? 1.0f : 0.0f);

    const float decayAmp = 3.0f * std::log (10.0f) / rt60;                    // decaimiento de amplitud
    const float tLate = (float) (lateFrames * hopSize) / (float) sampleRateHz;
    lateDecay = std::exp (-2.0f * decayAmp * tLate);                           // en potencia

    float* data[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numCh && ch < 2; ++ch)
        data[ch] = buffer.getWritePointer (ch);

    float blockIn = 0.0f, blockOut = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float mix = mixSmooth.getNextValue();
        const float byp = bypassSmooth.getNextValue();
        float inMax = 0.0f, outMax = 0.0f;

        for (int ch = 0; ch < numCh && ch < 2; ++ch)
        {
            const float x = data[ch][n];
            const float y = processSample (channels[(size_t) ch], x, mix, byp, delta);
            data[ch][n] = y;
            inMax  = juce::jmax (inMax,  std::abs (x));
            outMax = juce::jmax (outMax, std::abs (y));
        }

        blockIn  = juce::jmax (blockIn, inMax);
        blockOut = juce::jmax (blockOut, outMax);
        columnIn  = juce::jmax (columnIn, inMax);
        columnOut = juce::jmax (columnOut, outMax);

        if (++columnCounter >= samplesPerColumn)
        {
            const int h = telemetry.head.load (std::memory_order_relaxed);
            telemetry.inPeak[(size_t) h].store (columnIn, std::memory_order_relaxed);
            telemetry.outPeak[(size_t) h].store (columnOut, std::memory_order_relaxed);
            telemetry.head.store ((h + 1) % DereverbTelemetry::cols, std::memory_order_release);
            columnCounter = 0;
            columnIn = columnOut = 0.0f;
        }
    }

    // Medidores suavizados
    float red = 0.0f;
    for (int ch = 0; ch < numCh && ch < 2; ++ch)
        red += channels[(size_t) ch].lastReduction;
    red /= (float) juce::jmax (1, juce::jmin (numCh, 2));

    const float blockSeconds = (float) numSamples / (float) sampleRateHz;
    const float k = 1.0f - std::exp (-blockSeconds / 0.08f);
    reductionSmooth += k * (red - reductionSmooth);
    inLevelSmooth   = juce::jmax (blockIn,  inLevelSmooth  * std::exp (-blockSeconds / 0.15f));
    outLevelSmooth  = juce::jmax (blockOut, outLevelSmooth * std::exp (-blockSeconds / 0.15f));
    telemetry.reduction.store (reductionSmooth, std::memory_order_relaxed);
    telemetry.inLevel.store (inLevelSmooth, std::memory_order_relaxed);
    telemetry.outLevel.store (outLevelSmooth, std::memory_order_relaxed);
}

float DereverbProcessor::processSample (Channel& c, float x, float mix, float bypass, bool delta)
{
    // Senal seca retrasada por la latencia, para mezclar en fase
    c.dry[(size_t) c.dryPos] = x;
    const float dryDelayed = c.dry[(size_t) ((c.dryPos - latency + fftSize) % fftSize)];
    c.dryPos = (c.dryPos + 1) % fftSize;

    // FIFO de entrada / salida
    c.inFifo[(size_t) c.pos] = x;
    const float wet = c.outFifo[(size_t) c.pos];
    c.outFifo[(size_t) c.pos] = 0.0f;
    c.pos = (c.pos + 1) % fftSize;

    if (++c.hopCount >= hopSize)
    {
        c.hopCount = 0;
        processFrame (c);
    }

    const float processed = delta ? (dryDelayed - wet)                         // solo lo que se quita
                                  : dryDelayed * (1.0f - mix) + wet * mix;
    return processed + (dryDelayed - processed) * bypass;
}

void DereverbProcessor::processFrame (Channel& c)
{
    auto* d = c.fftData.data();

    // El frame empieza en la muestra mas antigua (c.pos)
    for (int i = 0; i < fftSize; ++i)
        d[i] = c.inFifo[(size_t) ((c.pos + i) % fftSize)] * window[(size_t) i];
    std::fill (d + fftSize, d + fftSize * 2, 0.0f);

    fft->performRealOnlyForwardTransform (d);     // espectro completo, intercalado re/im

    auto& old = c.history[(size_t) c.histIdx];    // potencia de hace 'lateFrames' frames

    double sumP = 0.0, sumGP = 0.0;

    for (int k = 0; k < numBins; ++k)
    {
        const float re = d[2 * k], im = d[2 * k + 1];
        const float power = re * re + im * im;

        // Potencia estimada de la reverb tardia: eco anterior atenuado por el decaimiento
        const float late = lateDecay * old[(size_t) k];

        // Sustraccion espectral (potencia) con suelo de reduccion
        const float g2 = 1.0f - amount * late / (power + 1.0e-12f);
        float g = std::sqrt (juce::jmax (g2, floorGain * floorGain));
        g = juce::jmin (g, 1.0f);

        // Suavizado temporal para evitar "ruido musical"
        auto& gp = c.gainPrev[(size_t) k];
        g = 0.5f * gp + 0.5f * g;
        gp = g;

        sumP  += power;
        sumGP += (double) g * (double) g * power;

        d[2 * k] *= g;
        d[2 * k + 1] *= g;
        if (k > 0 && k < fftSize / 2)              // espejo conjugado
        {
            d[2 * (fftSize - k)] *= g;
            d[2 * (fftSize - k) + 1] *= g;
        }

        old[(size_t) k] = power;                   // guarda la potencia original
    }
    c.histIdx = (c.histIdx + 1) % lateFrames;

    // Fraccion de energia eliminada en este frame (solo si hay senal)
    c.lastReduction = sumP > 1.0e-6 ? (float) (1.0 - std::sqrt (sumGP / sumP)) : 0.0f;

    fft->performRealOnlyInverseTransform (d);

    for (int i = 0; i < fftSize; ++i)
        c.outFifo[(size_t) ((c.pos + i) % fftSize)] += d[i] * window[(size_t) i] * norm;
}

juce::AudioProcessorEditor* DereverbProcessor::createEditor()
{
    return new DereverbEditor (*this);
}

void DereverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DereverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DereverbProcessor();
}
