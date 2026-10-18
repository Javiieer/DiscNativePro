# Avisos de terceros

Este inventario es parcial y no sustituye una revisión de licencias para distribución. El código del producto DiscNativePro no tiene una licencia de redistribución documentada; véase la sección «Licencia» de `README.md`.

## Signalsmith Stretch

- Archivos incluidos: `src/audio/thirdparty/signalsmith-stretch.h`
- Proyecto: https://github.com/Signalsmith-Audio/signalsmith-stretch
- Licencia declarada por upstream: MIT
- Copyright: Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.

## Signalsmith Linear

- Archivos incluidos: `src/audio/thirdparty/signalsmith-linear/stft.h` y `fft.h`
- Proyecto: https://github.com/Signalsmith-Audio/linear
- Licencia declarada por upstream: MIT
- Copyright: Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.

### MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Dependencias que todavía requieren un inventario completo

- JUCE 8.0.6 se descarga desde CMake; hay que confirmar el modelo/licencia aplicable a cada distribución.
- Microsoft WebView2 SDK se descarga para Windows; revisar sus términos y alinear la versión de CMake con los workflows.
- `yt-dlp` se descarga en tiempo de ejecución en algunas rutas; su binario y sus avisos/licencias deben acompañar cualquier paquete que lo incluya.
- Los builds Linux dependen de bibliotecas del sistema GTK, WebKitGTK, ALSA/JACK, X11 y otras; documentar sus versiones y obligaciones en un SBOM.
