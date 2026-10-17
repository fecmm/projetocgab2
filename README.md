# Cena 3D Interativa – Ache os objetos escondidos

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
| E | Interagir com o alvo (mira no centro da tela): pegar moeda, abrir/fechar armário, abrir/fechar gaveta, acender/apagar abajur |
| F1 | Alternar wireframe |
| ESC | Desistir (mostra o resultado parcial); ESC de novo fecha |

O contador de itens, o tempo e a dica de interação aparecem no título da janela.

## O que já está pronto

- Modelagem própria: cubo, cilindro e esfera com normais e UVs; mesa (com gaveta), cadeira, cama, armário, estante, abajur, tapete, moedas e sala
- Transformações: T·R·S com hierarquia (porta do armário em torno da dobradiça, gaveta deslizando, moedas girando e flutuando, estante rotacionada)
- Navegação: câmera FPS com colisão simples (paredes e móveis)
- Interações: armário, gaveta, abajur e coleta de itens
- Iluminação: Blinn-Phong com luz direcional fraca + luz pontual (abajur) e materiais distintos por objeto
- Texturas: madeira, piso, tijolo, tecido e metal geradas proceduralmente
- Jogo: 6 itens, 3 min, partida única; se o tempo acabar ou o jogador desistir, mostra o resultado parcial (ex.: 4/6)

## Falta fazer (ideias)

- HUD com texto/quads 2D em vez do título da janela
- Mais objetos e detalhes (ventilador de teto, janela, quadros), mais texturas (arquivos com stb_image)
- Sombras (shadow mapping), skybox, normal mapping, transparência no vidro
- Relatório (1–3 páginas) e vídeo de 2–4 min

## Estrutura

```
CMakeLists.txt
src/main.cpp
external/glad/    (você gera, ver acima)
```
