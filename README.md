# Cena 3D Interativa – Ache as moedas escondidas

Minijogo de partida única em OpenGL 3.3 Core (C++17, GLFW, GLAD, GLM e stb_image). 
O jogador explora um quarto à noite e deve achar as 8 moedas douradas escondidas antes de 3 minutos. Sem motores e sem modelos importados: toda a geometria é gerada em código.

**Equipe:** [Emile Noemi], [Fernanda Correia]
**Vídeo:** link do vídeo

## Como compilar
Instale CMake (3.16+), um compilador C++17 e Git. No Linux: `sudo apt install build-essential cmake git xorg-dev libgl1-mesa-dev`.

1. Gere o GLAD em https://glad.dav1d.de (Language C/C++, Specification OpenGL, API gl 3.3, Profile Core, marque "Generate a loader") e extraia o zip em `external/glad/`, de modo que existam `external/glad/include/glad/glad.h`, `external/glad/include/KHR/khrplatform.h` e `external/glad/src/glad.c`.
2. Compile (GLFW e GLM são baixados automaticamente pelo CMake):
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
| E | Pegar moeda (objeto na mira, no centro da tela) |
| F | Abrir/fechar armário, gaveta da mesa, gaveta do criado-mudo e baú |
| G | Ligar/desligar abajur e ventilador de teto |
| L | Ligar/desligar a lanterna |
| F1 | Alternar wireframe |
| ESC | Desistir (mostra o resultado parcial); ESC de novo fecha |

O contador de moedas, o tempo, a mira e a dica de interação aparecem na tela (HUD 2D).

## Requisitos
- **Modelagem própria:** cubo, cilindro e esfera gerados em código com normais e UVs
  (`makeCube`, `makeCylinder`, `makeSphere`). Com eles foram montados mesa (com gaveta),
  cadeira, cama, armário, estante, abajur, tapete, moedas, janela, quadros, ventilador de
  teto, criado-mudo, baú, vaso com planta e cesto de roupa.
- **Transformações:** translação, rotação e escala em hierarquia (porta do armário em torno da
  dobradiça, gavetas deslizando, tampa do baú, pás do ventilador, moedas girando e flutuando).
- **Navegação:** câmera FPS (W/A/S/D e mouse) com colisão com paredes e móveis.
- **Interações:** armário, gaveta da mesa, gaveta do criado-mudo, baú, abajur, ventilador e
  coleta de moedas.
- **Iluminação e materiais:** Blinn-Phong com materiais distintos por objeto; luz direcional
  (luar que entra pela janela, com sombras por shadow mapping), luz pontual (abajur) e
  spot light (lanterna).
- **Texturas:** procedurais (madeira, piso, tijolo, tecido e metal), imagens PNG carregadas com
  stb_image (tapete e quadros) e cubemap do céu noturno gerado em código.
- **Extras:** HUD 2D com fonte bitmap, tela inicial, skybox com estrelas e lua, vidro
  transparente.
- **Jogo:** 8 moedas, 3 minutos, partida única; se o tempo acabar ou o jogador desistir,
  mostra o resultado parcial (ex.: 5/8).

## Estrutura
```
CMakeLists.txt
src/main.cpp
assets/            texturas em PNG (tapete e quadros)
tools/             gerar_texturas.py: script que gera as imagens de assets/
external/glad/    (você gera, ver acima)
external/stb/      stb_image.h
docs/relatorio.pdf
```

## Bibliotecas: 
GLFW (janela, teclado e mouse), GLAD (carregador do OpenGL), GLM (matrizes e vetores) e
stb_image (leitura de PNG). Nenhuma é um motor de jogo.
