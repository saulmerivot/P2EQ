# P2EQ

EQ transiente/tonal com saturação (VST3 / AU / Standalone). By St. Punk! - https://stpunk.art

## Baixar o beta
Aba **Actions** > última execução verde > **Artifacts**: `P2EQ-mac` (VST3 + AU + app, universal) e `P2EQ-win64`.
Instruções de instalação vão dentro de cada zip (LEIA-ME).

## Compilar
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target PirateEQ2_VST3 PirateEQ2_Standalone
```
JUCE 8.0.8 é baixado automaticamente (ou use uma cópia em `./JUCE`).
Testes de DSP: alvo `SplitTool` (rode sem argumentos; 0 falhas esperado).

## Estrutura
- `Source/dsp/` split transiente/tonal (HPSS multi-resolução), EQ 8 bandas x 2 fluxos, saturação
- `Source/` processador, editor, skin em imagens (`assets/skin`)
- `Tools/` testes offline, A/B contra plugins de referência, renderização da UI

## Licença
JUCE é AGPLv3 ou comercial; este projeto usa JUCE sob **AGPLv3**, portanto o código é distribuído sob AGPLv3 (ver `LICENSE`).
Fonte Metal Mania: SIL OFL 1.1 (`assets/fonts/OFL-MetalMania.txt`).
