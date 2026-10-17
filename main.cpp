// Cena 3D interativa: minijogo "ache os objetos escondidos" (partida unica).
// OpenGL 3.3 Core + GLFW + GLAD + GLM. Sem motores, sem modelos importados:
// toda a geometria e todas as texturas sao geradas em codigo.

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using glm::mat4;
using glm::vec2;
using glm::vec3;

// ============================================================ shaders
static const char* VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel, uView, uProj;
uniform vec2 uUVScale;
out vec3 vPos; out vec3 vNormal; out vec2 vUV;
void main(){
    vec4 w = uModel * vec4(aPos, 1.0);
    vPos = w.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV * uUVScale;
    gl_Position = uProj * uView * w;
})";

static const char* FS = R"(#version 330 core
in vec3 vPos; in vec3 vNormal; in vec2 vUV;
out vec4 FragColor;
struct Material { vec3 ambient; vec3 diffuse; vec3 specular; float shininess; vec3 emissive; };
uniform Material uMat;
uniform sampler2D uTex;
uniform vec3 uViewPos;
uniform vec3 uAmbient;
uniform vec3 uSunDir;   uniform vec3 uSunColor;     // luz direcional
uniform vec3 uLampPos;  uniform vec3 uLampColor;    // luz pontual (abajur)
uniform int uLampOn;

vec3 blinn(vec3 L, vec3 lightColor, vec3 N, vec3 V, vec3 tex){
    float d = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float s = d > 0.0 ? pow(max(dot(N, H), 0.0), uMat.shininess) : 0.0;
    return lightColor * (uMat.diffuse * tex * d + uMat.specular * s);
}
void main(){
    vec3 tex = texture(uTex, vUV).rgb;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vPos);
    vec3 c = uMat.emissive + uMat.ambient * tex * uAmbient;
    c += blinn(normalize(-uSunDir), uSunColor, N, V, tex);
    if (uLampOn == 1) {
        vec3 Lv = uLampPos - vPos;
        float dist = length(Lv);
        float att = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        c += blinn(Lv / dist, uLampColor, N, V, tex) * att;
    }
    FragColor = vec4(c, 1.0);
})";

static GLuint gProg = 0;

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); std::fprintf(stderr, "Shader: %s\n", log); }
    return s;
}
static GLuint buildProgram() {
    GLuint v = compileShader(GL_VERTEX_SHADER, VS), f = compileShader(GL_FRAGMENT_SHADER, FS);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::fprintf(stderr, "Link: %s\n", log); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}
static GLint U(const char* n) { return glGetUniformLocation(gProg, n); }

// ============================================================ malhas (modelagem propria)
struct Vertex { vec3 p, n; vec2 uv; };
struct Mesh { GLuint vao = 0, vbo = 0, ebo = 0; GLsizei count = 0; };

static Mesh upload(const std::vector<Vertex>& v, const std::vector<unsigned>& idx) {
    Mesh m; m.count = (GLsizei)idx.size();
    glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo); glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, p));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, n));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glBindVertexArray(0);
    return m;
}

// Cubo unitario centrado na origem (24 vertices: normais e UVs por face)
static Mesh makeCube() {
    std::vector<Vertex> v; std::vector<unsigned> idx;
    struct Face { vec3 n, u, v; };
    const Face faces[6] = {
        {{1,0,0}, {0,0,-1}, {0,1,0}}, {{-1,0,0}, {0,0,1}, {0,1,0}},
        {{0,1,0}, {1,0,0}, {0,0,-1}}, {{0,-1,0}, {1,0,0}, {0,0,1}},
        {{0,0,1}, {1,0,0}, {0,1,0}},  {{0,0,-1}, {-1,0,0}, {0,1,0}}};
    for (const Face& f : faces) {
        unsigned b = (unsigned)v.size();
        vec3 c = f.n * 0.5f;
        v.push_back({c - f.u * 0.5f - f.v * 0.5f, f.n, {0, 0}});
        v.push_back({c + f.u * 0.5f - f.v * 0.5f, f.n, {1, 0}});
        v.push_back({c + f.u * 0.5f + f.v * 0.5f, f.n, {1, 1}});
        v.push_back({c - f.u * 0.5f + f.v * 0.5f, f.n, {0, 1}});
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    return upload(v, idx);
}

// Cilindro unitario (raio 0.5, altura 1) no eixo Y, com tampas
static Mesh makeCylinder(int seg = 32) {
    std::vector<Vertex> v; std::vector<unsigned> idx;
    const float PI = 3.14159265f;
    for (int i = 0; i <= seg; i++) {
        float a = 2 * PI * i / seg, cx = std::cos(a), sz = std::sin(a);
        v.push_back({{0.5f * cx, -0.5f, 0.5f * sz}, {cx, 0, sz}, {(float)i / seg, 0}});
        v.push_back({{0.5f * cx,  0.5f, 0.5f * sz}, {cx, 0, sz}, {(float)i / seg, 1}});
    }
    for (int i = 0; i < seg; i++) {
        unsigned a0 = 2 * i, b0 = 2 * i + 1, a1 = 2 * i + 2, b1 = 2 * i + 3;
        idx.insert(idx.end(), {a0, b1, a1, a0, b0, b1});
    }
    for (int cap = 0; cap < 2; cap++) {
        float y = cap ? 0.5f : -0.5f; vec3 n(0, cap ? 1.f : -1.f, 0);
        unsigned c = (unsigned)v.size();
        v.push_back({{0, y, 0}, n, {0.5f, 0.5f}});
        for (int i = 0; i <= seg; i++) {
            float a = 2 * PI * i / seg;
            v.push_back({{0.5f * std::cos(a), y, 0.5f * std::sin(a)}, n, {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}});
        }
        for (int i = 0; i < seg; i++) {
            if (cap) idx.insert(idx.end(), {c, c + 2 + (unsigned)i, c + 1 + (unsigned)i});
            else     idx.insert(idx.end(), {c, c + 1 + (unsigned)i, c + 2 + (unsigned)i});
        }
    }
    return upload(v, idx);
}

// Esfera de diametro 1 (latitude/longitude)
static Mesh makeSphere(int stacks = 20, int slices = 32) {
    std::vector<Vertex> v; std::vector<unsigned> idx;
    const float PI = 3.14159265f;
    for (int i = 0; i <= stacks; i++) {
        float phi = PI * i / stacks;
        for (int j = 0; j <= slices; j++) {
            float th = 2 * PI * j / slices;
            vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
            v.push_back({n * 0.5f, n, {(float)j / slices, (float)i / stacks}});
        }
    }
    for (int i = 0; i < stacks; i++)
        for (int j = 0; j < slices; j++) {
            unsigned k1 = i * (slices + 1) + j, k2 = k1 + slices + 1;
            idx.insert(idx.end(), {k1, k1 + 1, k2, k1 + 1, k2 + 1, k2});
        }
    return upload(v, idx);
}

// ============================================================ texturas procedurais
static float hash2(int x, int y) {
    unsigned n = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return ((n ^ (n >> 16)) & 0xFFFFu) / 65535.0f;
}
static GLuint makeTexture(int w, int h, const std::function<vec3(int, int)>& f) {
    std::vector<unsigned char> d((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            vec3 c = glm::clamp(f(x, y), 0.0f, 1.0f);
            for (int k = 0; k < 3; k++) d[((size_t)y * w + x) * 3 + k] = (unsigned char)(c[k] * 255.0f);
        }
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, d.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return t;
}
struct Textures { GLuint white, wood, floor, wall, fabric, metal; };
static Textures makeTextures() {
    Textures t;
    t.white = makeTexture(1, 1, [](int, int) { return vec3(1.0f); });
    t.wood = makeTexture(64, 64, [](int x, int y) {
        float g = 0.5f + 0.5f * std::sin(x * 0.35f + 3.0f * hash2(0, y / 4));
        vec3 c = glm::mix(vec3(0.38f, 0.22f, 0.10f), vec3(0.62f, 0.40f, 0.20f), g);
        return c * (0.92f + 0.08f * hash2(x, y));
    });
    t.floor = makeTexture(64, 64, [](int x, int y) {
        bool light = ((x / 16) + (y / 16)) % 2 == 0;
        vec3 c = light ? vec3(0.72f, 0.62f, 0.48f) : vec3(0.45f, 0.36f, 0.27f);
        if (x % 16 == 0 || y % 16 == 0) c *= 0.7f;
        return c * (0.94f + 0.06f * hash2(x, y));
    });
    t.wall = makeTexture(64, 64, [](int x, int y) {
        int row = y / 16, off = (row % 2) * 16;
        bool mortar = (y % 16) < 2 || ((x + off) % 32) < 2;
        if (mortar) return vec3(0.75f);
        return vec3(0.62f, 0.30f, 0.22f) * (0.85f + 0.15f * hash2((x + off) / 32, row));
    });
    t.fabric = makeTexture(32, 32, [](int x, int y) {
        float s = ((x + y) % 4 < 2) ? 1.0f : 0.88f;
        return vec3(s * (0.9f + 0.1f * hash2(x, y)));
    });
    t.metal = makeTexture(32, 32, [](int x, int y) {
        return vec3(0.75f + 0.1f * hash2(x, y / 2));
    });
    return t;
}

// ============================================================ materiais e desenho
struct Material { vec3 amb, diff, spec; float shin; vec3 emis; };
static Material mat(vec3 a, vec3 d, vec3 s, float sh, vec3 e = vec3(0)) { return {a, d, s, sh, e}; }

static const Material M_WOOD   = mat(vec3(0.5f), vec3(1.0f), vec3(0.15f), 16);       // madeira semi-fosca
static const Material M_FLOOR  = mat(vec3(0.5f), vec3(1.0f), vec3(0.35f), 48);       // piso polido
static const Material M_WALL   = mat(vec3(0.6f), vec3(1.0f), vec3(0.02f), 4);        // parede fosca
static const Material M_CEIL   = mat(vec3(0.6f), vec3(0.9f), vec3(0.0f), 2);
static const Material M_METAL  = mat(vec3(0.3f), vec3(0.6f), vec3(0.95f), 96);       // metal brilhante
static const Material M_GOLD   = mat(vec3(0.4f), vec3(1.0f, 0.82f, 0.2f), vec3(0.9f, 0.8f, 0.4f), 64, vec3(0.30f, 0.22f, 0.02f));
static Material fabricMat(vec3 tint) { return mat(vec3(0.5f), tint, vec3(0.03f), 4); }

static void draw(const Mesh& m, const mat4& model, const Material& mt, GLuint tex, vec2 uv = vec2(1.0f)) {
    glUniformMatrix4fv(U("uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform2fv(U("uUVScale"), 1, glm::value_ptr(uv));
    glUniform3fv(U("uMat.ambient"), 1, glm::value_ptr(mt.amb));
    glUniform3fv(U("uMat.diffuse"), 1, glm::value_ptr(mt.diff));
    glUniform3fv(U("uMat.specular"), 1, glm::value_ptr(mt.spec));
    glUniform1f(U("uMat.shininess"), mt.shin);
    glUniform3fv(U("uMat.emissive"), 1, glm::value_ptr(mt.emis));
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
}

// Hierarquia: parent * T(pos) * S(size)
static mat4 part(const mat4& parent, vec3 pos, vec3 size) {
    return glm::scale(glm::translate(parent, pos), size);
}

// ============================================================ estado do jogo e camera
struct Item { vec3 pos; int req; bool got; };   // req: 0 livre, 1 armario aberto, 2 gaveta aberta
struct Game {
    bool lampOn = true, ended = false, wireframe = false;
    float closet = 0, closetT = 0, drawer = 0, drawerT = 0;
    std::vector<Item> items;
    double start = 0, endTime = 0;
    std::string result;
} G;

static const double TIME_LIMIT = 180.0;

static vec3 camPos(0.0f, 1.7f, 3.0f);
static float yaw = -90.0f, pitch = 0.0f;
static bool pressE = false;

static vec3 camFront() {
    float y = glm::radians(yaw), p = glm::radians(pitch);
    return glm::normalize(vec3(std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)));
}
static int foundCount() { int n = 0; for (auto& i : G.items) n += i.got; return n; }

static void endGame(const char* motivo) {
    if (G.ended) return;
    G.ended = true;
    G.endTime = glfwGetTime() - G.start;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s | Itens: %d/%d | Tempo: %d:%02d | ESC para sair",
                  motivo, foundCount(), (int)G.items.size(), (int)G.endTime / 60, (int)G.endTime % 60);
    G.result = buf;
    std::printf("%s\n", buf);
}

static void keyCb(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) { if (!G.ended) endGame("Voce desistiu"); else glfwSetWindowShouldClose(w, 1); }
    if (key == GLFW_KEY_E) pressE = true;
    if (key == GLFW_KEY_F1) G.wireframe = !G.wireframe;
}
static void mouseCb(GLFWwindow*, double x, double y) {
    static bool first = true; static double lx = 0, ly = 0;
    if (first) { lx = x; ly = y; first = false; }
    double dx = x - lx, dy = ly - y; lx = x; ly = y;
    if (G.ended) return;
    yaw += (float)dx * 0.1f;
    pitch = glm::clamp(pitch + (float)dy * 0.1f, -89.0f, 89.0f);
}

// ---- colisao simples (circulo do jogador contra AABBs no plano XZ)
struct AABB { float x0, z0, x1, z1; };
static const AABB COLLIDERS[] = {
    {-3.35f, -3.65f, -1.65f, -2.75f},  // mesa
    { 2.15f, -3.15f,  3.85f, -0.85f},  // cama
    { 0.30f, -4.00f,  1.70f, -3.40f},  // armario
    {-3.95f,  0.40f, -3.65f,  1.60f},  // estante
};
static bool blocked(vec3 p) {
    const float R = 0.3f, L = 4.0f - R;
    if (std::fabs(p.x) > L || std::fabs(p.z) > L) return true;
    for (const AABB& a : COLLIDERS)
        if (p.x > a.x0 - R && p.x < a.x1 + R && p.z > a.z0 - R && p.z < a.z1 + R) return true;
    return false;
}

// ---- itens e interacao
static bool available(const Item& it) {
    if (it.req == 1) return G.closet > 0.7f;
    if (it.req == 2) return G.drawer > 0.2f;
    return true;
}
static vec3 itemPos(int i, double t) {
    vec3 p = G.items[i].pos;
    if (i == 1) p.z += G.drawer;            // acompanha a gaveta
    p.y += 0.02f * (float)std::sin(t * 2.0 + i);
    return p;
}
struct Target { int kind = -1; int idx = 0; float score = 1e9f; const char* label = ""; };
static Target pick(double t) {
    Target best; vec3 f = camFront();
    auto consider = [&](vec3 p, int kind, int idx, const char* label) {
        vec3 d = p - camPos; float dist = glm::length(d);
        if (dist > 2.6f || dist < 1e-3f || glm::dot(d / dist, f) < 0.85f) return;
        float score = dist - (kind == 0 ? 1.0f : 0.0f);   // itens tem prioridade
        if (score < best.score) { best.kind = kind; best.idx = idx; best.score = score; best.label = label; }
    };
    for (int i = 0; i < (int)G.items.size(); i++)
        if (!G.items[i].got && available(G.items[i])) consider(itemPos(i, t), 0, i, "[E] Pegar item");
    consider(vec3(1.0f, 1.1f, -3.4f), 1, 0, G.closetT > 0 ? "[E] Fechar armario" : "[E] Abrir armario");
    consider(vec3(-2.05f, 0.64f, -2.9f + G.drawer), 2, 0, G.drawerT > 0 ? "[E] Fechar gaveta" : "[E] Abrir gaveta");
    consider(vec3(-3.1f, 1.0f, -3.2f), 3, 0, G.lampOn ? "[E] Apagar abajur" : "[E] Acender abajur");
    return best;
}
static void interact(const Target& t) {
    switch (t.kind) {
        case 0: G.items[t.idx].got = true; break;
        case 1: G.closetT = G.closetT > 0 ? 0.0f : 1.8f; break;
        case 2: G.drawerT = G.drawerT > 0 ? 0.0f : 0.4f; break;
        case 3: G.lampOn = !G.lampOn; break;
    }
}

// ============================================================ cena
struct Meshes { Mesh cube, cyl, sph; };

static const vec3 LAMP_LIGHT_POS(-3.1f, 1.15f, -3.2f);

static void drawScene(const Meshes& M, const Textures& T, double t) {
    const mat4 I(1.0f);
    // --- sala: piso, teto, paredes
    draw(M.cube, part(I, {0, -0.05f, 0}, {8.2f, 0.1f, 8.2f}), M_FLOOR, T.floor, {8, 8});
    draw(M.cube, part(I, {0, 3.05f, 0}, {8.2f, 0.1f, 8.2f}), M_CEIL, T.fabric, {4, 4});
    draw(M.cube, part(I, {0, 1.5f, -4.05f}, {8.2f, 3, 0.1f}), M_WALL, T.wall, {8, 3});
    draw(M.cube, part(I, {0, 1.5f,  4.05f}, {8.2f, 3, 0.1f}), M_WALL, T.wall, {8, 3});
    draw(M.cube, part(I, { 4.05f, 1.5f, 0}, {0.1f, 3, 8.2f}), M_WALL, T.wall, {8, 3});
    draw(M.cube, part(I, {-4.05f, 1.5f, 0}, {0.1f, 3, 8.2f}), M_WALL, T.wall, {8, 3});

    // --- tapete
    draw(M.cube, part(I, {0, 0.01f, 0.5f}, {2.5f, 0.02f, 1.8f}), fabricMat({0.85f, 0.35f, 0.35f}), T.fabric, {6, 4});

    // --- mesa (tampo + 4 pernas) com gaveta deslizante e abajur
    mat4 D = glm::translate(I, vec3(-2.5f, 0, -3.2f));
    draw(M.cube, part(D, {0, 0.75f, 0}, {1.6f, 0.06f, 0.8f}), M_WOOD, T.wood);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            draw(M.cube, part(D, {0.72f * sx, 0.36f, 0.32f * sz}, {0.06f, 0.72f, 0.06f}), M_WOOD, T.wood);
    float off = G.drawer;                                       // translacao animada
    draw(M.cube, part(D, {0.45f, 0.58f, off}, {0.5f, 0.03f, 0.6f}), M_WOOD, T.wood);
    draw(M.cube, part(D, {0.45f, 0.64f, 0.315f + off}, {0.5f, 0.14f, 0.03f}), M_WOOD, T.wood);
    draw(M.cube, part(D, {0.215f, 0.64f, off}, {0.03f, 0.1f, 0.6f}), M_WOOD, T.wood);
    draw(M.cube, part(D, {0.685f, 0.64f, off}, {0.03f, 0.1f, 0.6f}), M_WOOD, T.wood);
    // abajur: base + haste (metal) + cupula (emissiva quando ligada)
    mat4 L = glm::translate(D, vec3(-0.6f, 0.78f, 0));
    draw(M.cyl, part(L, {0, 0.02f, 0}, {0.22f, 0.04f, 0.22f}), M_METAL, T.metal);
    draw(M.cyl, part(L, {0, 0.27f, 0}, {0.03f, 0.5f, 0.03f}), M_METAL, T.metal);
    Material shade = mat(vec3(0.6f), vec3(1.0f, 0.95f, 0.8f), vec3(0.05f), 8,
                         G.lampOn ? vec3(0.9f, 0.8f, 0.5f) : vec3(0));
    draw(M.sph, part(L, {0, 0.37f, 0}, {0.3f, 0.22f, 0.3f}), shade, T.fabric);

    // --- cadeira
    mat4 H = glm::translate(I, vec3(-2.5f, 0, -2.2f));
    draw(M.cube, part(H, {0, 0.45f, 0}, {0.5f, 0.06f, 0.5f}), M_WOOD, T.wood);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            draw(M.cube, part(H, {0.22f * sx, 0.21f, 0.22f * sz}, {0.05f, 0.42f, 0.05f}), M_WOOD, T.wood);
    draw(M.cube, part(H, {0, 0.73f, 0.23f}, {0.5f, 0.5f, 0.05f}), M_WOOD, T.wood);

    // --- cama (pernas, estrado, colchao, travesseiro, cabeceira)
    mat4 B = glm::translate(I, vec3(3.0f, 0, -2.0f));
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            draw(M.cube, part(B, {0.7f * sx, 0.225f, 1.0f * sz}, {0.08f, 0.45f, 0.08f}), M_WOOD, T.wood);
    draw(M.cube, part(B, {0, 0.51f, 0}, {1.6f, 0.12f, 2.2f}), M_WOOD, T.wood);
    draw(M.cube, part(B, {0, 0.67f, 0}, {1.5f, 0.2f, 2.1f}), fabricMat({0.55f, 0.65f, 1.0f}), T.fabric, {4, 4});
    draw(M.sph,  part(B, {0, 0.84f, -0.8f}, {0.6f, 0.16f, 0.35f}), fabricMat({1.0f, 1.0f, 0.95f}), T.fabric, {2, 2});
    draw(M.cube, part(B, {0, 0.9f, -1.12f}, {1.6f, 0.9f, 0.06f}), M_WOOD, T.wood);

    // --- armario (corpo + porta articulada por dobradica: rotacao em torno de Y)
    mat4 C = glm::translate(I, vec3(1.0f, 0, -3.7f));
    draw(M.cube, part(C, {0, 1.1f, -0.28f}, {1.4f, 2.2f, 0.04f}), M_WOOD, T.wood);
    draw(M.cube, part(C, {-0.68f, 1.1f, 0}, {0.04f, 2.2f, 0.6f}), M_WOOD, T.wood);
    draw(M.cube, part(C, { 0.68f, 1.1f, 0}, {0.04f, 2.2f, 0.6f}), M_WOOD, T.wood);
    draw(M.cube, part(C, {0, 2.18f, 0}, {1.4f, 0.04f, 0.6f}), M_WOOD, T.wood);
    draw(M.cube, part(C, {0, 0.02f, 0}, {1.4f, 0.04f, 0.6f}), M_WOOD, T.wood);
    mat4 rod = glm::rotate(glm::translate(C, vec3(0, 1.8f, 0)), glm::radians(90.0f), vec3(0, 0, 1));
    draw(M.cyl, glm::scale(rod, vec3(0.03f, 1.3f, 0.03f)), M_METAL, T.metal);
    mat4 door = glm::rotate(glm::translate(C, vec3(-0.7f, 0, 0.3f)), -G.closet, vec3(0, 1, 0));
    draw(M.cube, part(door, {0.7f, 1.1f, 0}, {1.4f, 2.2f, 0.04f}), M_WOOD, T.wood);
    draw(M.sph,  part(door, {1.25f, 1.1f, 0.05f}, {0.07f, 0.07f, 0.07f}), M_METAL, T.metal);

    // --- estante encostada na parede oeste (rotacionada 90 graus)
    mat4 S = glm::rotate(glm::translate(I, vec3(-3.8f, 0, 1.0f)), glm::radians(90.0f), vec3(0, 1, 0));
    draw(M.cube, part(S, {-0.6f, 0.9f, 0}, {0.04f, 1.8f, 0.3f}), M_WOOD, T.wood);
    draw(M.cube, part(S, { 0.6f, 0.9f, 0}, {0.04f, 1.8f, 0.3f}), M_WOOD, T.wood);
    draw(M.cube, part(S, {0, 0.9f, -0.14f}, {1.2f, 1.8f, 0.02f}), M_WOOD, T.wood);
    for (float y : {0.04f, 0.6f, 1.2f, 1.78f})
        draw(M.cube, part(S, {0, y, 0}, {1.2f, 0.04f, 0.3f}), M_WOOD, T.wood);
    const vec3 bookTint[5] = {{0.8f, 0.2f, 0.2f}, {0.2f, 0.5f, 0.8f}, {0.2f, 0.7f, 0.3f}, {0.9f, 0.7f, 0.2f}, {0.6f, 0.3f, 0.7f}};
    for (int i = 0; i < 5; i++)
        draw(M.cube, part(S, {-0.45f + 0.15f * i, 0.76f, 0}, {0.1f, 0.28f, 0.2f}), fabricMat(bookTint[i]), T.fabric);

    // --- itens: moedas giratorias (rotacao animada) e flutuantes
    for (int i = 0; i < (int)G.items.size(); i++) {
        if (G.items[i].got || !available(G.items[i])) continue;
        mat4 m = glm::translate(I, itemPos(i, t));
        m = glm::rotate(m, (float)t * 1.5f, vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(90.0f), vec3(1, 0, 0));
        draw(M.cyl, glm::scale(m, vec3(0.2f, 0.03f, 0.2f)), M_GOLD, T.metal);
    }
}

// ============================================================ main
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Cena 3D", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "Falha ao criar janela\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, keyCb);
    glfwSetCursorPosCallback(win, mouseCb);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::fprintf(stderr, "Falha no GLAD\n"); return 1; }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    gProg = buildProgram();
    Meshes M{makeCube(), makeCylinder(), makeSphere()};
    Textures T = makeTextures();

    G.items = {
        {{1.0f, 0.1f, -3.7f}, 1, false},    // dentro do armario
        {{-2.05f, 0.66f, -3.2f}, 2, false}, // dentro da gaveta
        {{2.45f, 0.1f, -2.0f}, 0, false},   // embaixo da cama
        {{-3.8f, 1.28f, 0.8f}, 0, false},   // na estante
        {{3.6f, 0.1f, 3.6f}, 0, false},     // no canto escuro
        {{1.0f, 2.32f, -3.7f}, 0, false},   // em cima do armario
    };
    G.start = glfwGetTime();
    double last = G.start, titleTimer = 0;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime(), dt = now - last; last = now;
        glfwPollEvents();

        // ---- jogo
        if (!G.ended) {
            if (TIME_LIMIT - (now - G.start) <= 0) endGame("Tempo esgotado");
            else if (foundCount() == (int)G.items.size()) endGame("Voce venceu!");
        }

        // ---- movimento WASD com colisao (eixos separados)
        if (!G.ended) {
            vec3 fwd = glm::normalize(vec3(camFront().x, 0, camFront().z));
            vec3 right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
            vec3 dir(0);
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) dir += fwd;
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) dir -= fwd;
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) dir += right;
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) dir -= right;
            if (glm::length(dir) > 0) {
                float speed = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 4.5f : 2.5f);
                vec3 d = glm::normalize(dir) * speed * (float)dt, tp = camPos;
                tp.x += d.x; if (!blocked(tp)) camPos = tp;
                tp = camPos; tp.z += d.z; if (!blocked(tp)) camPos = tp;
            }
        }

        // ---- interacao
        Target tgt = G.ended ? Target() : pick(now);
        if (pressE && tgt.kind >= 0) interact(tgt);
        pressE = false;

        // ---- animacoes (interpolacao suave ate o alvo)
        auto approach = [&](float& v, float target, float speed) {
            float step = speed * (float)dt, d = target - v;
            v += glm::clamp(d, -step, step);
        };
        approach(G.closet, G.closetT, 2.5f);
        approach(G.drawer, G.drawerT, 0.8f);

        // ---- render
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        if (h == 0) h = 1;
        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.06f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, G.wireframe ? GL_LINE : GL_FILL);

        glUseProgram(gProg);
        mat4 view = glm::lookAt(camPos, camPos + camFront(), vec3(0, 1, 0));
        mat4 proj = glm::perspective(glm::radians(70.0f), (float)w / h, 0.05f, 50.0f);
        glUniformMatrix4fv(U("uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(U("uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform1i(U("uTex"), 0);
        glUniform3fv(U("uViewPos"), 1, glm::value_ptr(camPos));
        glUniform3f(U("uAmbient"), 0.22f, 0.22f, 0.28f);
        glUniform3f(U("uSunDir"), -0.3f, -1.0f, -0.4f);
        glUniform3f(U("uSunColor"), 0.18f, 0.18f, 0.25f);
        glUniform3fv(U("uLampPos"), 1, glm::value_ptr(LAMP_LIGHT_POS));
        glUniform3f(U("uLampColor"), 2.0f, 1.7f, 1.2f);
        glUniform1i(U("uLampOn"), G.lampOn ? 1 : 0);
        drawScene(M, T, now);

        // ---- "HUD" no titulo da janela (ate implementar texto/quads 2D)
        titleTimer += dt;
        if (titleTimer > 0.15) {
            titleTimer = 0;
            char buf[256];
            if (G.ended) std::snprintf(buf, sizeof buf, "%s", G.result.c_str());
            else {
                int left = (int)std::max(0.0, TIME_LIMIT - (now - G.start));
                std::snprintf(buf, sizeof buf, "Itens %d/%d | Tempo %d:%02d | %s",
                              foundCount(), (int)G.items.size(), left / 60, left % 60, tgt.kind >= 0 ? tgt.label : "");
            }
            glfwSetWindowTitle(win, buf);
        }
        glfwSwapBuffers(win);
    }
    glfwTerminate();
    return 0;
}
