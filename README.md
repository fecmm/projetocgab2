# Cena 3D Interativa – Ache as moedas escondidos

Minijogo de partida única em OpenGL 3.3 Core (C++17, GLFW, GLAD, GLM). O jogador explora um quarto e deve achar
as 6 moedas douradas escondidas antes de 3 minutos. Sem motores e sem modelos importados: toda a geometria e as
texturas são geradas em código.

## Como compilar

1. Instale CMake (3.16+), um compilador C++17 e Git. No Linux: `sudo apt install build-essential cmake git xorg-dev libgl1-mesa-dev`.
2. Gere o GLAD em https://glad.dav1d.de (Language C/C++, Specification OpenGL, API gl 3.3, Profile Core,
   marque "Generate a loader") e extraia o zip em `external/glad/`, de modo que existam
   `external/glad/include/glad/glad.h`, `external/glad/include/KHR/khrplatform.h` e `external/glad/src/glad.c`.
3. Compile (GLFW e GLM são baixados automaticamente pelo CMake):

```
cmake -S . -B build
cmake --build build --config Release
./build/cena          # Windows: build\Release\cena.exe
```

## Comandos

| Tecla / mouse | Ação |
|---|---|
| W / A / S / D | Andar |
| Shift | Correr |
| Mouse | Olhar |
| ENTER | Começar o jogo (tela inicial) |
| E | Interagir com o alvo (mira no centro da tela): pegar moeda |
| F | Abrir/fechar armário e gaveta |
| F | Abrir/fechar armário e gaveta |
| F1 | Alternar wireframe |
| ESC | Desistir (mostra o resultado parcial); ESC de novo fecha |

O contador de itens, o tempo e a dica de interação aparecem no título da janela.

## Requisitos

- Modelagem própria: mesa, cadeira, cama, armário, estante, abajur, tapete, moedas, janela, quadros, ventilador, mesa de cabeçeira, vaso de planta, cesta de roupas e baú 
- Transformações: T·R·S com hierarquia (porta do armário em torno da dobradiça, gaveta deslizando, moedas girando e flutuando, estante rotacionada) 
- Navegação: câmera FPS com colisão simples (paredes e móveis) 
- Interações: armário, gaveta, abajur e coleta de itens, ventilador 
- Iluminação: Blinn-Phong com luz direcional fraca + luz pontual (abajur) e materiais distintos por objeto + , sombras, lanterna 
- Texturas: madeira, piso, tijolo, tecido e metal geradas proceduralmente 
- Jogo: 8 itens, 3 min, partida única; se o tempo acabar ou o jogador desistir, mostra o resultado parcial (ex.: 4/6) 

## Estrutura

```
CMakeLists.txt
src/main.cpp
assets/            texturas em PNG (geradas por tools/gerar_texturas.py)
tools/             script que gera as texturas
external/glad/    (você gera, ver acima)
external/stb/      stb_image.h
```

## Bibliotecas: 
GLFW, GLAD, GLM, stb_image.
