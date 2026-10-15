# Dereverb

Plugin de audio (VST3 / AU / Standalone, hecho con JUCE) que reduce la reverberación de sala
mediante supresión espectral de la cola tardía.

## Interfaz

La ventana es redimensionable (mantiene la proporción) y se anima con el audio:

- **Drying Field:** muestra el audio real. A la izquierda entra la señal con su reverb, en el
  centro el campo la "seca" y a la derecha sale la onda limpia. Los fragmentos que flotan son
  la energía que el algoritmo está quitando.
- **Cantidad (esfera vertical):** cuánta reverb se resta. Doble clic = valor por defecto.
- **RT60 sala (s):** tiempo de reverberación aproximado de la sala.
- **Reducción max (dB):** perilla; limita cuánto se atenúa cada banda.
- **Mezcla:** proporción de señal procesada.
- **Power (arriba a la derecha):** enciende o apaga el efecto.
- **Botón rojo (derecha):** modo **Delta**, para escuchar solo lo que se está quitando.
- Doble clic en cualquier caja de valor para escribir un número.

El plasma, los tubos y el líquido se mueven según el nivel de entrada y salida. Si el equipo va
justo de CPU, los efectos bajan solos su resolución.

## Parámetros

| Parámetro | Descripción |
|---|---|
| Cantidad | Cuánta reverb se resta (1 = neutro, más alto = más agresivo) |
| RT60 sala (s) | Tiempo de reverberación aproximado de la sala (0.2 – 3 s) |
| Reducción max (dB) | Límite de atenuación por banda, evita que suene "hueco" |
| Mezcla | Proporción entre señal procesada y original |
| Bypass | Desactiva el efecto (con latencia compensada) |
| Delta | Escucha solo lo que se quita |

Latencia: 768 muestras (el plugin la informa al DAW).

## Estructura

- `PluginProcessor.*`: el algoritmo y los datos que alimentan la interfaz.
- `PluginEditor.*`: la interfaz.
- `assets/skin.jpg`: la ilustración de fondo (con los controles vaciados).
- `assets/fxmask.png`: máscara de efectos (R = líneas luminosas, G = núcleo del plasma,
  B = rampa para pulsos que viajan). Las dos imágenes se incrustan en el binario.

## Compilar en GitHub

1. Sube este proyecto a un repositorio de GitHub (rama `main`), con la carpeta `assets` y la
   carpeta oculta `.github` incluidas.
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
