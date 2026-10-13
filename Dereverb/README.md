# Dereverb

Plugin de audio (VST3 / AU / Standalone, hecho con JUCE) que reduce la reverberación de sala
mediante supresión espectral de la cola tardía.

## Parámetros

| Parámetro | Descripción |
|---|---|
| Cantidad | Cuánta reverb se resta (1 = neutro, más alto = más agresivo) |
| RT60 sala (s) | Tiempo de reverberación aproximado de la sala (0.2 – 3 s) |
| Reducción max (dB) | Límite de atenuación por banda, evita que suene "hueco" |
| Mezcla | Proporción entre señal procesada y original |

Latencia: 768 muestras (el plugin la informa al DAW).

## Compilar en GitHub

1. Sube este proyecto a un repositorio de GitHub (rama `main`).
2. Ve a la pestaña **Actions**. El workflow **Build** corre solo en cada push
   (también se puede lanzar a mano con *Run workflow*).
3. Cuando termine, entra a la ejecución y descarga los artefactos
   `Dereverb-Windows`, `Dereverb-macOS` y `Dereverb-Linux`.

## Instalar el plugin

- **Windows:** copia `Dereverb.vst3` a `C:\Program Files\Common Files\VST3\`
- **macOS:** copia `Dereverb.vst3` a `~/Library/Audio/Plug-Ins/VST3/` (y `Dereverb.component` a `~/Library/Audio/Plug-Ins/Components/`).
  Los binarios de Actions no están firmados; si macOS los bloquea, ejecuta
  `xattr -cr Dereverb.vst3` en la Terminal.
- **Linux:** copia `Dereverb.vst3` a `~/.vst3/`

## Compilar en local

Requiere CMake 3.22+ y un compilador C++17:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
