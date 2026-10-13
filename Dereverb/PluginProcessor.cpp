#include "PluginProcessor.h"

namespace
{
    constexpr float lateStartSeconds = 0.05f;   // la "cola tardía" empieza a los 50 ms
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

    // Ventana raíz-Hann periódica (análisis y síntesis)
    window.assign ((size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        const float hann = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));
        window[(size_t) i] = std::sqrt (hann);
    }

    // Normalización: suma de w^2 de los 4 frames solapados
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
    }

    setLatencySamples (latency);
}

void DereverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Parámetros -> coeficientes
    amount    = amountParam->load();
    floorGain = juce::Decibels::decibelsToGain (floorParam->load());
    const float rt60 = rt60Param->load();
    const float mix  = mixParam->load();

    const float delta = 3.0f * std::log (10.0f) / rt60;                       // decaimiento de amplitud
    const float tLate = (float) (lateFrames * hopSize) / (float) sampleRateHz;
    lateDecay = std::exp (-2.0f * delta * tLate);                              // en potencia

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& c = channels[(size_t) ch];
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            data[n] = processSample (c, data[n], mix);
    }
}

float DereverbProcessor::processSample (Channel& c, float x, float mix)
{
    // Señal seca retrasada por la latencia, para mezclar en fase
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

    return dryDelayed * (1.0f - mix) + wet * mix;
}

void DereverbProcessor::processFrame (Channel& c)
{
    auto* d = c.fftData.data();

    // El frame empieza en la muestra más antigua (c.pos)
    for (int i = 0; i < fftSize; ++i)
        d[i] = c.inFifo[(size_t) ((c.pos + i) % fftSize)] * window[(size_t) i];
    std::fill (d + fftSize, d + fftSize * 2, 0.0f);

    fft->performRealOnlyForwardTransform (d);     // espectro completo, intercalado re/im

    auto& old = c.history[(size_t) c.histIdx];    // potencia de hace 'lateFrames' frames

    for (int k = 0; k < numBins; ++k)
    {
        const float re = d[2 * k], im = d[2 * k + 1];
        const float power = re * re + im * im;

        // Potencia estimada de la reverb tardía: eco anterior atenuado por el decaimiento
        const float late = lateDecay * old[(size_t) k];

        // Sustracción espectral (potencia) con suelo de reducción
        const float g2 = 1.0f - amount * late / (power + 1.0e-12f);
        float g = std::sqrt (juce::jmax (g2, floorGain * floorGain));
        g = juce::jmin (g, 1.0f);

        // Suavizado temporal para evitar "ruido musical"
        auto& gp = c.gainPrev[(size_t) k];
        g = 0.5f * gp + 0.5f * g;
        gp = g;

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

    fft->performRealOnlyInverseTransform (d);

    for (int i = 0; i < fftSize; ++i)
        c.outFifo[(size_t) ((c.pos + i) % fftSize)] += d[i] * window[(size_t) i] * norm;
}

juce::AudioProcessorEditor* DereverbProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
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
