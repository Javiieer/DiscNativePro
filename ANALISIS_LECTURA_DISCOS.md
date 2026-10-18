# Análisis profundo de lectura de discos y pistas

Fecha: 2026-10-18
Alcance: revisión estática de `DiscNativePro-Enhanced`; se compiló Windows Release y se abrió la interfaz, pero no se cargó ni reprodujo audio. Las recomendaciones JUCE se contrastaron con el código JUCE 8 fijado dentro de `build/_deps`.

## Resumen

La lectura no es un único flujo. Hay tres rutas que se cruzan:

1. **Catálogo de archivos:** enumera carpetas y extrae tags para llenar la biblioteca.
2. **Análisis musical:** BPM, tonalidad y forma de onda; parte ocurre en segundo plano, pero el deck repite análisis síncrono al cargar.
3. **Reproducción:** `AudioFormatReaderSource` alimenta `BufferingAudioSource`, luego `ResamplingAudioSource`, y el callback de audio pide bloques a cada deck.

El retraso de inicio tenía una causa concreta: el constructor oculto de la biblioteca detectaba D: como disco “USB”, lo seleccionaba y recorría todos sus archivos antes de mostrar la ventana. Ya se eliminó ese recorrido de inicio. La lectura recursiva sigue siendo síncrona cuando se elige una unidad o carpeta desde la vista USB; ese es un riesgo de uso, no un bloqueo del arranque tras este cambio.

## 1. Enumeración y exploración de volúmenes

### Antes del cambio

- `src/services/UsbDriveScanner.cpp:71-95` usaba `GetDriveTypeW`. Cualquier volumen `DRIVE_FIXED` distinto de C: se clasificaba como unidad secundaria/USB. En este equipo D: es un volumen fijo de datos, así que entraba en esa lista.
- `src/ui/UsbLibraryView.cpp:58-143` construía la vista y llamaba `refreshDrives()` desde su constructor. La vista se crea aunque esté oculta, desde `src/ui/LibraryComponent.cpp:663`.
- `refreshDrives()` seleccionaba la primera unidad cuando no había una selección previa (`src/ui/UsbLibraryView.cpp:175-250`). `selectDrive()` empezaba inmediatamente el escaneo (`:318-329`).
- `src/Main.cpp` antes construía `MainComponent` antes de llamar `setVisible(true)`. La cadena anterior hacía que el inventario de D: pudiera retrasar la primera ventana.

### Estado después de este cambio

- Windows solo detecta automáticamente volúmenes `DRIVE_REMOVABLE`; ya no mete discos fijos internos como D: (`src/services/UsbDriveScanner.cpp:78-85`). Discos externos que Windows presenta como `DRIVE_FIXED` se pueden elegir con el botón de carpeta manual.
- `UsbLibraryView` ya no enumera ni monitorea unidades al construirse (`src/ui/UsbLibraryView.cpp:139-145`). La enumeración ocurre al entrar a la vista USB o pulsar refrescar. La vista lista las unidades y espera que el usuario pulse una antes de leer sus contenidos (`:175-318`).
- La ventana de inicio ligera aparece antes de construir la interfaz; el audio/MIDI se inicializa después de que la interfaz principal está montada (`src/Main.cpp:15-29,86-114`; `src/ui/MainComponent.cpp:208-217`).
- El escaneo de la carpeta local de Descargas se aplaza 300 ms después de construir la biblioteca (`src/ui/LibraryComponent.cpp:839-848`). SoundCloud ya no solicita una búsqueda de red predeterminada al iniciar (`src/ui/SoundCloudLibraryView.cpp:80-86`).

## 2. Cómo se construye el catálogo USB

Al pulsar una unidad, `scanDriveFiles()` ejecuta `rootDir.findChildFiles(..., true)` (`src/ui/UsbLibraryView.cpp:366-375`). Eso materializa primero en memoria la lista completa de archivos y solo después filtra extensiones de audio. No hay progreso, cancelación, límite de profundidad, ni exclusión explícita de enlaces o carpetas de sistema en esa ruta.

Por cada archivo de audio, abre un `AudioFormatReader` para duración y tags (`:383-435`). La lectura de metadatos ocurre en el hilo que atiende la interfaz. En un volumen lento, una estructura con miles de archivos, un lector que tarde o una unidad que se desconecte puede dejar la vista congelada. El usuario ya tiene que elegir la unidad para iniciar esto, pero la operación puede ser larga e interrumpible solo cerrando la app.

La vista USB asigna `124 BPM` y `8A` por defecto (`:414-415`) y no manda las pistas sin tags al analizador de biblioteca. Por tanto, esos números pueden parecer medidos cuando son valores fabricados. El lector Media Foundation de Windows tampoco llena `metadataValues`, así que algunos M4A/AAC muestran título y artista de reserva.

La biblioteca local recorre carpetas de forma síncrona con un filtro de extensiones (`src/ui/LibraryComponent.cpp:960-992`). Cada `addTrack()` abre reader, lee metadatos y, si faltan BPM o key, encola análisis (`:996-1090`). La carpeta de Descargas que se carga al inicio es superficial, no recursiva (`:996-1011`), y ahora se ejecuta después de que la ventana ya se mostró; todavía lee metadatos de cada archivo en el hilo de UI.

## 3. BPM, tonalidad y forma de onda

La biblioteca tiene `BackgroundBpmScanner`, que abre cada archivo en su thread y llama a `BeatDetector`/`KeyDetector` (`src/ui/LibraryComponent.cpp:558-617`). Sin embargo:

- el destructor espera 2 segundos y luego detiene el thread (`:860-872`); los bucles de análisis no comprueban una señal de cancelación. JUCE puede terminar forzosamente el thread al agotarse el timeout, incluso mientras un reader está vivo.
- el callback que publica resultados usa `[this]` dentro de `MessageManager::callAsync` (`:615-617`), por lo que existe riesgo de uso después de liberar la vista si el callback queda pendiente.
- la carga del deck vuelve a ejecutar análisis de BPM, key y waveform en la ruta síncrona: `DeckComponent::loadAudioFile()` llama `deck.loadFile()` (`src/ui/DeckComponent.cpp:295-298`); `DeckPlayer::loadFile()` analiza hasta 60 segundos y procesa la forma de onda antes de terminar (`src/audio/DeckPlayer.cpp:33-58,571-621`). Esto bloquea el hilo que inició la carga, generalmente la UI, y puede repetir análisis que ya hizo la biblioteca.

## 4. Camino de reproducción y garantías de tiempo real

El deck construye `AudioFormatReaderSource` → `BufferingAudioSource` → `ResamplingAudioSource` (`src/audio/DeckPlayer.cpp:90-107`). Los dos decks comparten un `TimeSliceThread` de prioridad normal (`src/audio/AudioEngine.cpp:6-8,27-28`; `src/audio/AudioEngine.h:109`).

En la versión JUCE incluida, el hilo de lectura mantiene `callbackLock` mientras lee del `AudioSource` (`build/_deps/juce-src/modules/juce_audio_basics/sources/juce_BufferingAudioSource.cpp:325-334`). El callback adquiere ese mismo lock al copiar la caché (`:113-128`). Así que una lectura lenta puede hacer que el callback espere por disco; además, los dos decks comparten el mismo hilo de lectura. La caché reduce los cortes habituales, pero no garantiza que el callback nunca espere.

La caché se crea sin prellenado (`DeckPlayer.cpp:94-97`). JUCE puede iniciar reproducción antes de que el trabajador haya llenado un bloque, y devolver silencio si no hay muestras disponibles. También hay `CriticalSection` propia dentro del procesamiento del deck (`:144`), que añade otra exclusión mutua al callback.

### Frecuencias de muestreo

`DeckPlayer` calcula duración con `reader->sampleRate` (`DeckPlayer.cpp:43`), pero las posiciones para seek/getPosition se convierten usando `currentSampleRate` del dispositivo (`:412-425`). `AudioFormatReaderSource` pasa frames del formato tal cual; no convierte la frecuencia (`build/_deps/juce-src/modules/juce_audio_formats/format/juce_AudioFormatReaderSource.cpp:59-89`). `ResamplingAudioSource` empieza en ratio 1 (`DeckPlayer.cpp:99-107`) y luego usa el ratio de pitch (`:458-475`), sin una relación base entre frecuencia del archivo y del dispositivo. Si una pista está a 44.1 kHz y el dispositivo a 48 kHz, velocidad, seek, loops y posición final pueden quedar mal.

JUCE también puede ampliar el buffer interno de `ResamplingAudioSource` desde `getNextAudioBlock()` si el ratio crece por encima de la capacidad preparada (`build/_deps/juce-src/modules/juce_audio_basics/sources/juce_ResamplingAudioSource.cpp:58-72,109-118`). Con pitch elevado y Key Lock apagado puede haber asignación de memoria dentro del callback.

## 5. Lector M4A/AAC de Media Foundation

El lector propio de Windows (`src/audio/MediaFoundationAudioFormat_win.h`) contiene riesgos específicos del formato:

- Llama `MFStartup` al crear cada reader, pero no se ve un `MFShutdown` correspondiente en el destructor (`:25,73-76`).
- Ignora HRESULT de `SetCurrentPosition` y actualiza de todos modos su posición (`:87-98`).
- Una lectura parcial puede reportarse como éxito sin rellenar de silencio el resto del bloque (`:176`); `AudioFormatReader::read()` confía en el booleano (`build/_deps/juce-src/modules/juce_audio_formats/format/juce_AudioFormatReader.cpp:98-101`). Un error puede dejar muestras residuales o no inicializadas.
- `residualBuffer.erase(begin, begin + channels)` por frame convierte el vaciado en costo cuadrático cuando hay exceso de muestras (`:103-115`); `push_back` también puede reasignar memoria durante la lectura de fondo (`:165-170`).
- Convertir PCM firmado con `int16_t << 16` en muestras negativas es comportamiento indefinido en C++ (`:110,159`).
- No carga tags de Media Foundation, por eso los M4A pueden perder título/artista/BPM/key aunque el archivo tenga metadata.

Estos puntos se limitan a la ruta Media Foundation/M4A/AAC; no se deben atribuir automáticamente a WAV, MP3 o FLAC.

## 6. Priorización recomendada para la siguiente fase

1. **Primero, escaneo cancelable en segundo plano:** recorrido incremental de directorios, filtrado durante la enumeración, batches a la interfaz, progreso y cancelación; no materializar todos los archivos del volumen en un `Array`.
2. **Después, caché de metadata:** guardar tags/duración y huella de archivo (ruta, tamaño y fecha de cambio) para no volver a abrir lectores al cada refresh.
3. **Carga del deck no bloqueante:** abrir/preparar el reader en un worker cancelable y publicar la nueva pista solo cuando esté lista; analizar waveform/BPM/key por separado y desechar resultados de cargas anteriores.
4. **Audio sin espera de disco:** sustituir la dependencia de un lock compartido en el callback por búfer circular preasignado por deck. El callback solo consume muestras ya listas; en underrun emite silencio y suma un contador.
5. **Corrección de tiempo y formato:** aplicar la conversión archivo↔dispositivo además del pitch, definir posiciones en una sola unidad (segundos o frames de origen) y probar 44.1/48 kHz, seeks, loops y pitch extremo.
6. **Endurecer Media Foundation:** equilibrar inicialización/destrucción, comprobar HRESULT, limpiar bloques parciales, evitar operaciones cuadráticas y convertir PCM con aritmética definida. Mostrar metadata ausente como “desconocida”; no inventar BPM/key.

## Estado de la revisión

- **Corregido ahora:** la ventana se presenta antes de cargar la UI completa; el audio empieza después; abre primero dos salidas e intenta cuatro solo si el dispositivo anuncia al menos cuatro; los discos fijos secundarios dejan de aparecer como USB; no hay escaneo USB automático al arrancar ni búsqueda automática de SoundCloud.
- **Sigue pendiente deliberadamente:** mover el escaneo manual USB/local, la lectura de metadata y la carga de decks a workers cancelables; arreglar el pipeline RT/sample-rate y el lector Media Foundation. Se separa como próxima fase porque el pedido actual priorizó el inicio/audio y solicitó analizar en profundidad el diseño existente.
- No se abrieron archivos de música ni se reprodujeron canciones durante esta comprobación.
