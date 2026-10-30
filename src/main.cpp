// Cena 3D interativa: minijogo "ache os objetos escondidos" (partida unica).
// OpenGL 3.3 Core + GLFW + GLAD + GLM. Sem motores, sem modelos importados:
// toda a geometria e todas as texturas sao geradas em codigo.

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;


// ============================================================ shaders
static const char* VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel, uView, uProj;
uniform vec2 uUVScale;
uniform mat4 uLightSpace;  
out vec3 vPos; out vec3 vNormal; out vec2 vUV; out vec4 vLightPos;
void main(){
    vec4 w = uModel * vec4(aPos, 1.0);
    vPos = w.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV * uUVScale;
    gl_Position = uProj * uView * w;
    vLightPos = uLightSpace * w;
})";

static const char* FS = R"(#version 330 core
in vec3 vPos; in vec3 vNormal; in vec2 vUV; in vec4 vLightPos;
out vec4 FragColor;
struct Material { vec3 ambient; vec3 diffuse; vec3 specular; float shininess; vec3 emissive; };
uniform Material uMat;
uniform sampler2D uTex;
uniform vec3 uViewPos;
uniform vec3 uAmbient;
uniform vec3 uSunDir;   uniform vec3 uSunColor;     // luz direcional
uniform vec3 uLampPos;  uniform vec3 uLampColor;    // luz pontual (abajur)
uniform int uLampOn;
uniform float uAlpha;

uniform sampler2D uShadowMap;                                   // mapa de sombra da lua
uniform int uFlashOn; uniform vec3 uFlashPos; uniform vec3 uFlashDir;   // lanterna (spot light)

// 1 = iluminado, 0 = na sombra 
float shadowFactor(vec3 N, vec3 L){
    vec3 p = vLightPos.xyz / vLightPos.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0006);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++) {
            float closest = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;
            lit += (p.z - bias <= closest) ? 1.0 : 0.0;
        }
    return lit / 9.0;
}

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
    vec3 Ls = normalize(-uSunDir);
    c += blinn(Ls, uSunColor, N, V, tex) * shadowFactor(N, Ls);   // luar com sombra
    if (uLampOn == 1) {
        vec3 Lv = uLampPos - vPos;
        float dist = length(Lv);
        float att = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        c += blinn(Lv / dist, uLampColor, N, V, tex) * att;
    }
    
    if (uFlashOn == 1) {                                          // lanterna: cone com borda suave
        vec3 Lf = uFlashPos - vPos;
        float dist = length(Lf);
        vec3 Ld = Lf / dist;
        float theta = dot(Ld, normalize(-uFlashDir));
        float k = clamp((theta - 0.85) / (0.93 - 0.85), 0.0, 1.0);
        float att = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        c += blinn(Ld, vec3(1.6, 1.6, 1.4), N, V, tex) * att * k;
    }
    
    FragColor = vec4(c, uAlpha);
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

static GLuint loadTextureFile(const std::string& name) {
    const std::string path = std::string(ASSET_DIR) + "/" + name;
    int w = 0, h = 0, n = 0;
    stbi_set_flip_vertically_on_load(1);   
    unsigned char* d = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!d) {
        std::fprintf(stderr, "Falha ao carregar textura: %s\n", path.c_str());
        return makeTexture(1, 1, [](int, int) { return vec3(0.8f); });   // cinza claro no lugar
    }
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, d);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(d);
    return t;
}

struct Textures { GLuint white, wood, floor, wall, fabric, metal, rug, paint1, paint2; };
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

    t.rug    = loadTextureFile("tapete.png");
    t.paint1 = loadTextureFile("quadro1.png");
    t.paint2 = loadTextureFile("quadro2.png");

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
static const Material M_GLASS  = mat(vec3(0.5f), vec3(0.55f, 0.75f, 0.95f), vec3(0.9f), 128, vec3(0.05f, 0.08f, 0.12f));

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

// ============================================================ skybox (ceu noturno gerado em codigo)
static const char* SKY_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uView, uProj;
out vec3 vDir;
void main(){
    vDir = aPos;                                 // a posicao do cubo serve de direcao
    mat4 v = mat4(mat3(uView));                  // tira a translacao: o ceu acompanha a camera
    gl_Position = uProj * v * vec4(aPos, 1.0);
})";
static const char* SKY_FS = R"(#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uSky;
void main(){ FragColor = vec4(texture(uSky, vDir).rgb, 1.0); })";

static GLuint gSkyProg = 0, gSkyTex = 0;
static const vec3 MOON_DIR = glm::normalize(vec3(1.0f, 0.45f, 0.1f));   // direcao da lua no ceu

static GLuint buildSkyProgram() {
    GLuint v = compileShader(GL_VERTEX_SHADER, SKY_VS), f = compileShader(GL_FRAGMENT_SHADER, SKY_FS);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::fprintf(stderr, "Link sky: %s\n", log); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// Gera as 6 faces do cubemap: gradiente de noite, estrelas e uma lua
static GLuint makeSkybox() {
    const int S = 256;
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_CUBE_MAP, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int face = 0; face < 6; face++) {
        std::vector<unsigned char> d((size_t)S * S * 3);
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                float a = 2.0f * (x + 0.5f) / S - 1.0f, b = 2.0f * (y + 0.5f) / S - 1.0f;
                vec3 dir;
                switch (face) {   // convencao do OpenGL para cada face do cubemap
                    case 0:  dir = vec3( 1, -b, -a); break;   // +X
                    case 1:  dir = vec3(-1, -b,  a); break;   // -X
                    case 2:  dir = vec3( a,  1,  b); break;   // +Y
                    case 3:  dir = vec3( a, -1, -b); break;   // -Y
                    case 4:  dir = vec3( a, -b,  1); break;   // +Z
                    default: dir = vec3(-a, -b, -1); break;   // -Z
                }
                dir = glm::normalize(dir);
                float up = glm::clamp(dir.y, 0.0f, 1.0f);
                vec3 c = glm::mix(vec3(0.10f, 0.13f, 0.26f), vec3(0.01f, 0.02f, 0.07f), std::sqrt(up));   // horizonte -> zenite
                if (dir.y < 0.0f) c = vec3(0.02f, 0.03f, 0.05f);                                          // chao distante
                if (hash2(face * S + x, y) > 0.996f && dir.y > 0.0f)                                       // estrelas
                    c += vec3(0.5f + 0.5f * hash2(x, y + 7));
                float cs = glm::dot(dir, MOON_DIR);
                c += vec3(1.0f, 1.0f, 0.9f) * (0.35f * std::pow(std::max(cs, 0.0f), 200.0f) + 0.10f * std::pow(std::max(cs, 0.0f), 25.0f));  // brilho
                if (cs > 0.9982f) c = vec3(0.95f, 0.95f, 0.85f);                                           // disco da lua
                c = glm::clamp(c, 0.0f, 1.0f);
                for (int k = 0; k < 3; k++) d[((size_t)y * S + x) * 3 + k] = (unsigned char)(c[k] * 255.0f);
            }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB, S, S, 0, GL_RGB, GL_UNSIGNED_BYTE, d.data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return t;
}

// ============================================================ sombras (shadow mapping da luz da lua)
static const char* DEPTH_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel, uLightSpace;
void main(){ gl_Position = uLightSpace * uModel * vec4(aPos, 1.0); })";
static const char* DEPTH_FS = R"(#version 330 core
void main(){ })";

static const int SHADOW_SIZE = 2048;
static GLuint gShadowFbo = 0, gShadowTex = 0, gDepthProg = 0;
static bool gShadowPass = false;                                        // true enquanto desenha o mapa de sombra
static const vec3 SUN_DIR = glm::normalize(vec3(-1.0f, -0.45f, -0.1f)); // luar entrando pela janela (leste para oeste)

static void initShadow() {
    GLuint v = compileShader(GL_VERTEX_SHADER, DEPTH_VS), f = compileShader(GL_FRAGMENT_SHADER, DEPTH_FS);
    gDepthProg = glCreateProgram();
    glAttachShader(gDepthProg, v); glAttachShader(gDepthProg, f); glLinkProgram(gDepthProg);
    glDeleteShader(v); glDeleteShader(f);
    glGenTextures(1, &gShadowTex); glBindTexture(GL_TEXTURE_2D, gShadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1, 1, 1, 1};                               // fora do mapa = iluminado
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glGenFramebuffers(1, &gShadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gShadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gShadowTex, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);                       // so profundidade, sem cor
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::puts("Framebuffer de sombra incompleto");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================ estado do jogo e camera
struct Item { vec3 pos; int req; bool got; };   // req: 0 livre, 1 armario aberto, 2 gaveta aberta
struct Game {
    bool lampOn = true, flashOn = false, started = false, ended = false, wireframe = false;
    float closet = 0, closetT = 0, drawer = 0, drawerT = 0;
    float nightD = 0, nightDT = 0;   // gaveta do criado-mudo
    float chest = 0, chestT = 0;     // tampa do bau
    std::vector<Item> items;
    double start = 0, endTime = 0;
    std::string result;
    std::string reason;  // motivo do fim, usado na tela final do HUD
    bool fanOn = false;        // ventilador de teto ligado?
    float fanAngle = 0;
} G;

static const double TIME_LIMIT = 180.0;

static vec3 camPos(0.0f, 1.7f, 3.0f);
static float yaw = -90.0f, pitch = 0.0f;
static bool pressE = false;
static bool pressF = false;
static bool pressG = false;


static vec3 camFront() {
    float y = glm::radians(yaw), p = glm::radians(pitch);
    return glm::normalize(vec3(std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)));
}
static int foundCount() { int n = 0; for (auto& i : G.items) n += i.got; return n; }

static void endGame(const char* motivo) {
    if (G.ended) return;
    G.ended = true;
    G.reason = motivo;
    G.endTime = glfwGetTime() - G.start;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s | Itens: %d/%d | Tempo: %d:%02d | ESC para sair",
                  motivo, foundCount(), (int)G.items.size(), (int)G.endTime / 60, (int)G.endTime % 60);
    G.result = buf;
    std::printf("%s\n", buf);
}

static void keyCb(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (!G.started) {
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) { G.started = true; G.start = glfwGetTime(); }
        else if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, 1);
        return;
    }
    if (key == GLFW_KEY_ESCAPE) { if (!G.ended) endGame("Voce desistiu"); else glfwSetWindowShouldClose(w, 1); }
    if (key == GLFW_KEY_E) pressE = true;
    if (key == GLFW_KEY_F) pressF = true;
    if (key == GLFW_KEY_G) pressG = true;
    if (key == GLFW_KEY_L) G.flashOn = !G.flashOn;
    if (key == GLFW_KEY_F1) G.wireframe = !G.wireframe;
}
static void mouseCb(GLFWwindow*, double x, double y) {
    static bool first = true; static double lx = 0, ly = 0;
    if (first) { lx = x; ly = y; first = false; }
    double dx = x - lx, dy = ly - y; lx = x; ly = y;
    if (G.ended || !G.started) return;
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
    { 1.70f, -3.20f,  2.20f, -2.70f},  // criado-mudo
    { 3.35f,  1.30f,  3.95f,  2.30f},  // bau
    { 3.10f,  3.10f,  3.50f,  3.50f},  // vaso
    {-3.75f, -1.25f, -3.25f, -0.75f},  // cesto
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
    if (it.req == 4) return G.nightD > 0.2f;       // moeda no criado-mudo
    if (it.req == 5) return G.chest > 0.6f;        // moeda no bau
    return true;
}
static vec3 itemPos(int i, double t) {
    vec3 p = G.items[i].pos;
    if (i == 1) p.z += G.drawer;  
    if (i == 6) p.z += G.nightD;              // acompanha a gaveta
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
    consider(vec3(1.0f, 1.1f, -3.4f), 1, 0, G.closetT > 0 ? "[F] Fechar armario" : "[F] Abrir armario");
    consider(vec3(-2.05f, 0.64f, -2.9f + G.drawer), 2, 0, G.drawerT > 0 ? "[F] Fechar gaveta" : "[F] Abrir gaveta");
    consider(vec3(-3.1f, 1.0f, -3.2f), 3, 0, G.lampOn ? "[G] Apagar abajur" : "[G] Acender abajur");
    consider(vec3(0.0f, 2.7f, 0.5f), 5, 0, G.fanOn ? "[G] Desligar ventilador" : "[G] Ligar ventilador");
    consider(vec3(1.95f, 0.4f, -2.7f + G.nightD), 7, 0, G.nightDT > 0 ? "[F] Fechar gaveta" : "[F] Abrir gaveta");
    consider(vec3(3.5f, 0.45f, 1.8f), 8, 0, G.chestT > 0 ? "[F] Fechar bau" : "[F] Abrir bau");
    return best;
}
static void interact(const Target& t) {
    switch (t.kind) {
        case 0: G.items[t.idx].got = true; break;
        case 1: G.closetT = G.closetT > 0 ? 0.0f : 1.8f; break;
        case 2: G.drawerT = G.drawerT > 0 ? 0.0f : 0.4f; break;
        case 3: G.lampOn = !G.lampOn; break;
        case 5: G.fanOn = !G.fanOn; break;
        case 7: G.nightDT = G.nightDT > 0 ? 0.0f : 0.3f; break;
        case 8: G.chestT = G.chestT > 0 ? 0.0f : 1.0f; break;
    }
}

// ============================================================ HUD 2D
static const char* HUD_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;      // posicao em pixels
layout(location=1) in vec4 aColor;
uniform vec2 uScreen;
out vec4 vColor;
void main(){
    vec2 ndc = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
})";

static const char* HUD_FS = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main(){ FragColor = vColor; })";

static GLuint gHudProg = 0, gHudVao = 0, gHudVbo = 0;
static std::vector<float> gHudVerts;


struct Glyph { char c; unsigned char r[7]; };
static const Glyph FONT[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}}, {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {':', {0x00,0x04,0x04,0x00,0x04,0x04,0x00}}, {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
    {'[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}}, {']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}}, {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
};

static void hudInit() {
    GLuint v = compileShader(GL_VERTEX_SHADER, HUD_VS), f = compileShader(GL_FRAGMENT_SHADER, HUD_FS);
    gHudProg = glCreateProgram();
    glAttachShader(gHudProg, v); glAttachShader(gHudProg, f); glLinkProgram(gHudProg);
    GLint ok = 0; glGetProgramiv(gHudProg, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(gHudProg, 1024, nullptr, log); std::fprintf(stderr, "Link HUD: %s\n", log); }
    glDeleteShader(v); glDeleteShader(f);
    glGenVertexArrays(1, &gHudVao); glGenBuffers(1, &gHudVbo);
    glBindVertexArray(gHudVao);
    glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}


static void hudRect(float x, float y, float w, float h, vec4 c) {
    const float q[6][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y}, {x + w, y + h}, {x, y + h}};
    for (const auto& p : q) gHudVerts.insert(gHudVerts.end(), {p[0], p[1], c.r, c.g, c.b, c.a});
}
static float hudTextWidth(const std::string& s, float sc) { return s.empty() ? 0.0f : (s.size() * 6 - 1) * sc; }
static void hudGlyphs(float x, float y, float sc, const std::string& s, vec4 c) {
    for (char ch : s) {
        ch = (char)std::toupper((unsigned char)ch);
        for (const Glyph& g : FONT)
            if (g.c == ch) {
                for (int row = 0; row < 7; row++)
                    for (int col = 0; col < 5; col++)
                        if (g.r[row] & (1 << (4 - col))) hudRect(x + col * sc, y + row * sc, sc, sc, c);
                break;
            }
        x += 6 * sc;
    }
}
static void hudText(float x, float y, float sc, const std::string& s, vec4 c) {
    hudGlyphs(x + sc, y + sc, sc, s, vec4(0, 0, 0, 0.6f * c.a));
    hudGlyphs(x, y, sc, s, c);
}
static void hudTextCentered(float cx, float y, float sc, const std::string& s, vec4 c) {
    hudText(cx - hudTextWidth(s, sc) / 2, y, sc, s, c);
}
static void hudFlush(int w, int h) {
    if (gHudVerts.empty()) return;
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glUseProgram(gHudProg);
    glUniform2f(glGetUniformLocation(gHudProg, "uScreen"), (float)w, (float)h);
    glBindVertexArray(gHudVao);
    glBindBuffer(GL_ARRAY_BUFFER, gHudVbo);
    glBufferData(GL_ARRAY_BUFFER, gHudVerts.size() * sizeof(float), gHudVerts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(gHudVerts.size() / 6));
    glBindVertexArray(0);
    glDisable(GL_BLEND); glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);
    gHudVerts.clear();
}

static std::string mmss(int sec) {
    char b[16]; std::snprintf(b, sizeof b, "%d:%02d", sec / 60, sec % 60);
    return b;
}

static void drawHud(int w, int h, double now, const Target& tgt) {
    const float sc = std::max(2.0f, std::floor(h / 240.0f));   
    const float pad = 6 * sc, line = 7 * sc;
    const vec4 white(1, 1, 1, 1), gold(1.0f, 0.82f, 0.2f, 1), red(1.0f, 0.35f, 0.3f, 1), panel(0, 0, 0, 0.45f);
    const int total = (int)G.items.size();

    if (!G.started) {
        hudRect(0, 0, (float)w, (float)h, vec4(0, 0, 0, 0.75f));
        const float cx = w / 2.0f;
        float y = h * 0.12f;
        hudTextCentered(cx, y, sc * 2, "ACHE AS MOEDAS", gold);
        y += 3.5f * line;
        hudTextCentered(cx, y, sc, "ENCONTRE AS " + std::to_string(total) + " MOEDAS ESCONDIDAS EM " + std::to_string((int)(TIME_LIMIT / 60)) + " MINUTOS", white);
        y += 3.0f * line;
        const char* cmds[] = {
            "W A S D: ANDAR", "SHIFT: CORRER", "MOUSE: OLHAR", "E: PEGAR MOEDA",
            "F: ABRIR / FECHAR", "G: LIGAR / DESLIGAR", "L: LANTERNA", "F1: WIREFRAME", "ESC: DESISTIR"
        };
        for (const char* c : cmds) { hudTextCentered(cx, y, sc, c, white); y += 1.8f * line; }
        y += 1.5f * line;
        float blink = 0.6f + 0.4f * (float)std::sin(now * 4.0);
        hudTextCentered(cx, y, sc, "PRESSIONE ENTER PARA COMECAR", vec4(gold.r, gold.g, gold.b, blink));
        return;
    }

    if (!G.ended) {
        int left = (int)std::max(0.0, std::ceil(TIME_LIMIT - (now - G.start)));
        // canto superior esquerdo: moedas
        std::string a = "MOEDAS: " + std::to_string(foundCount()) + "/" + std::to_string(total);
        hudRect(pad, pad, hudTextWidth(a, sc) + 2 * pad, line + 2 * pad, panel);
        hudText(2 * pad, 2 * pad, sc, a, gold);
        // canto superior direito: tempo (vermelho nos ultimos 30 s)
        std::string b = "TEMPO: " + mmss(left);
        float bw = hudTextWidth(b, sc);
        hudRect(w - bw - 3 * pad, pad, bw + 2 * pad, line + 2 * pad, panel);
        hudText(w - bw - 2 * pad, 2 * pad, sc, b, left <= 30 ? red : white);
        // mira no centro
        float cx = w / 2.0f, cy = h / 2.0f;
        hudRect(cx - 7, cy - 1, 14, 2, vec4(1, 1, 1, 0.85f));
        hudRect(cx - 1, cy - 7, 2, 14, vec4(1, 1, 1, 0.85f));
        // dica de interacao abaixo da mira
        if (tgt.kind >= 0) {
            std::string t = tgt.label;
            float tw = hudTextWidth(t, sc);
            hudRect(cx - tw / 2 - pad, cy + 5 * sc - pad, tw + 2 * pad, line + 2 * pad, panel);
            hudTextCentered(cx, cy + 5 * sc, sc, t, white);
        }
    } else {
        // tela final: escurece a cena e mostra o resultado
        hudRect(0, 0, (float)w, (float)h, vec4(0, 0, 0, 0.65f));
        float cx = w / 2.0f, y = h / 2.0f - 18 * sc;
        bool win = foundCount() == total;
        hudTextCentered(cx, y, sc * 2, G.reason, win ? gold : red);
        hudTextCentered(cx, y + 3.2f * line, sc, "MOEDAS: " + std::to_string(foundCount()) + "/" + std::to_string(total), white);
        hudTextCentered(cx, y + 4.6f * line, sc, "TEMPO: " + mmss((int)G.endTime), white);
        hudTextCentered(cx, y + 6.6f * line, sc, "ESC PARA SAIR", vec4(0.8f, 0.8f, 0.8f, 1));
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

    // parede leste em 4 pecas, deixando uma abertura para a janela
    draw(M.cube, part(I, {4.05f, 1.5f, -1.475f}, {0.1f, 3.0f, 5.25f}), M_WALL, T.wall, {5.25f, 3});   // ate a janela
    draw(M.cube, part(I, {4.05f, 1.5f,  3.275f}, {0.1f, 3.0f, 1.65f}), M_WALL, T.wall, {1.65f, 3});   // depois da janela
    draw(M.cube, part(I, {4.05f, 0.5f,  1.8f},   {0.1f, 1.0f, 1.3f}),  M_WALL, T.wall, {1.3f, 1});    // abaixo da janela
    draw(M.cube, part(I, {4.05f, 2.6f,  1.8f},   {0.1f, 0.8f, 1.3f}),  M_WALL, T.wall, {1.3f, 0.8f}); // acima da janela

    draw(M.cube, part(I, {-4.05f, 1.5f, 0}, {0.1f, 3, 8.2f}), M_WALL, T.wall, {8, 3});

    // --- tapete
    draw(M.cube, part(I, {0, 0.01f, 0.5f}, {2.5f, 0.02f, 1.8f}), fabricMat(vec3(1.0f)), T.rug);

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
    mat4 B = glm::translate(I, vec3(3.0f, 0, -2.50f));
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            draw(M.cube, part(B, {0.7f * sx, 0.225f, 1.0f * sz}, {0.08f, 0.45f, 0.08f}), M_WOOD, T.wood);
    draw(M.cube, part(B, {0, 0.51f, 0}, {1.6f, 0.12f, 2.2f}), M_WOOD, T.wood);
    draw(M.cube, part(B, {0, 0.67f, 0}, {1.5f, 0.2f, 2.1f}), fabricMat({0.55f, 0.65f, 1.0f}), T.fabric, {4, 4});
    draw(M.sph,  part(B, {0, 0.84f, -0.8f}, {0.6f, 0.16f, 0.35f}), fabricMat({1.0f, 1.0f, 0.95f}), T.fabric, {2, 2});
    draw(M.cube, part(B, {0, 0.9f, -1.12f}, {1.6f, 0.9f, 0.06f}), M_WOOD, T.wood);

    // --- armario (corpo + porta articulada por dobradica: rotacao em torno de Y)
    mat4 C = glm::translate(I, vec3(0.85f, 0, -3.7f));
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

    // --- janela na parede leste (moldura de madeira, travessas e vidro)
    mat4 W = glm::translate(I, vec3(3.96f, 1.6f, 1.8f));
    draw(M.cube, part(W, {0,  0.6f, 0}, {0.06f, 0.06f, 1.3f}), M_WOOD, T.wood);        // barra de cima
    draw(M.cube, part(W, {0, -0.6f, 0}, {0.06f, 0.06f, 1.3f}), M_WOOD, T.wood);        // barra de baixo
    draw(M.cube, part(W, {0, 0, -0.62f}, {0.06f, 1.2f, 0.06f}), M_WOOD, T.wood);       // lateral
    draw(M.cube, part(W, {0, 0,  0.62f}, {0.06f, 1.2f, 0.06f}), M_WOOD, T.wood);       // lateral
    draw(M.cube, part(W, {0, 0, 0}, {0.05f, 1.2f, 0.04f}), M_WOOD, T.wood);            // travessa vertical
    draw(M.cube, part(W, {0, 0, 0}, {0.05f, 0.04f, 1.3f}), M_WOOD, T.wood);            // travessa horizontal
    draw(M.cube, part(W, {-0.04f, -0.64f, 0}, {0.14f, 0.04f, 1.45f}), M_WOOD, T.wood); // parapeito
    
    // --- quadros na parede norte (moldura + tela com ceu, chao e sol)
    auto quadro = [&](vec3 pos, GLuint tex) {
        mat4 Q = glm::translate(I, pos);
        draw(M.cube, part(Q, {0, 0, 0}, {0.9f, 0.7f, 0.04f}), M_WOOD, T.wood);                       // moldura
        draw(M.cube, part(Q, {0, 0, 0.022f}, {0.78f, 0.58f, 0.01f}), fabricMat(vec3(1.0f)), tex);    // tela com a imagem
    };
    quadro({-0.7f, 1.7f, -3.98f}, T.paint1);
    quadro({3.0f, 1.8f, -3.98f}, T.paint2);

    // --- ventilador de teto (haste, motor e 4 pas que giram em torno de Y)
    draw(M.cyl, part(I, {0.0f, 2.88f, 0.5f}, {0.04f, 0.24f, 0.04f}), M_METAL, T.metal);
    draw(M.cyl, part(I, {0.0f, 2.74f, 0.5f}, {0.24f, 0.1f, 0.24f}), M_METAL, T.metal);
    mat4 F = glm::rotate(glm::translate(I, vec3(0.0f, 2.7f, 0.5f)), glm::radians(G.fanAngle), vec3(0, 1, 0));
    for (int k = 0; k < 4; k++) {
        mat4 Bl = glm::rotate(F, glm::radians(90.0f * k), vec3(0, 1, 0));
        draw(M.cube, part(Bl, {0.42f, 0, 0}, {0.55f, 0.015f, 0.12f}), M_WOOD, T.wood);
    }

    // --- criado-mudo ao lado da cama (corpo aberto na frente + gaveta que desliza)
    mat4 K = glm::translate(I, vec3(1.90f, 0, -3.45f));
    draw(M.cube, part(K, {0, 0.60f, 0}, {0.5f, 0.04f, 0.5f}), M_WOOD, T.wood);              // tampo
    draw(M.cube, part(K, {-0.23f, 0.29f, 0}, {0.04f, 0.58f, 0.46f}), M_WOOD, T.wood);       // lateral
    draw(M.cube, part(K, { 0.23f, 0.29f, 0}, {0.04f, 0.58f, 0.46f}), M_WOOD, T.wood);       // lateral
    draw(M.cube, part(K, {0, 0.29f, -0.22f}, {0.42f, 0.58f, 0.04f}), M_WOOD, T.wood);       // fundo
    draw(M.cube, part(K, {0, 0.07f, 0}, {0.42f, 0.04f, 0.46f}), M_WOOD, T.wood);            // base
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            draw(M.cube, part(K, {0.2f * sx, 0.025f, 0.2f * sz}, {0.05f, 0.05f, 0.05f}), M_WOOD, T.wood);   // pes
    float no = G.nightD;                                                                    // translacao animada
    draw(M.cube, part(K, {0, 0.22f, no}, {0.38f, 0.02f, 0.42f}), M_WOOD, T.wood);           // gaveta: fundo
    draw(M.cube, part(K, {-0.19f, 0.28f, no}, {0.02f, 0.12f, 0.42f}), M_WOOD, T.wood);
    draw(M.cube, part(K, { 0.19f, 0.28f, no}, {0.02f, 0.12f, 0.42f}), M_WOOD, T.wood);
    draw(M.cube, part(K, {0, 0.28f, -0.2f + no}, {0.38f, 0.12f, 0.02f}), M_WOOD, T.wood);
    draw(M.cube, part(K, {0, 0.30f, 0.235f + no}, {0.42f, 0.18f, 0.03f}), M_WOOD, T.wood);  // frente
    draw(M.sph,  part(K, {0, 0.30f, 0.26f + no}, {0.04f, 0.04f, 0.04f}), M_METAL, T.metal); // puxador

    // --- bau sob a janela (corpo aberto em cima + tampa articulada: rotacao em torno do eixo Z)
    mat4 Cb = glm::translate(I, vec3(3.65f, 0, 1.8f));
    draw(M.cube, part(Cb, {0, 0.03f, 0}, {0.6f, 0.06f, 1.0f}), M_WOOD, T.wood);             // fundo
    draw(M.cube, part(Cb, {-0.27f, 0.2f, 0}, {0.06f, 0.4f, 1.0f}), M_WOOD, T.wood);         // frente
    draw(M.cube, part(Cb, { 0.27f, 0.2f, 0}, {0.06f, 0.4f, 1.0f}), M_WOOD, T.wood);         // lado da parede
    draw(M.cube, part(Cb, {0, 0.2f, -0.47f}, {0.54f, 0.4f, 0.06f}), M_WOOD, T.wood);        // lateral
    draw(M.cube, part(Cb, {0, 0.2f,  0.47f}, {0.54f, 0.4f, 0.06f}), M_WOOD, T.wood);        // lateral
    draw(M.cube, part(Cb, {-0.31f, 0.34f, 0}, {0.03f, 0.08f, 0.12f}), M_METAL, T.metal);    // fecho
    mat4 Lid = glm::rotate(glm::translate(Cb, vec3(0.3f, 0.4f, 0)), -G.chest * glm::radians(75.0f), vec3(0, 0, 1));
    draw(M.cube, part(Lid, {-0.3f, 0.03f, 0}, {0.6f, 0.06f, 1.0f}), M_WOOD, T.wood);        // tampa
    draw(M.cube, part(Lid, {-0.3f, 0.065f, -0.3f}, {0.62f, 0.01f, 0.06f}), M_METAL, T.metal);   // cintas de metal
    draw(M.cube, part(Lid, {-0.3f, 0.065f,  0.3f}, {0.62f, 0.01f, 0.06f}), M_METAL, T.metal);

    // --- vaso com planta no canto sudeste (a moeda do canto fica parcialmente atras dele)
    mat4 Vs = glm::translate(I, vec3(3.3f, 0, 3.3f));
    draw(M.cyl, part(Vs, {0, 0.17f, 0}, {0.3f, 0.34f, 0.3f}), fabricMat({0.75f, 0.4f, 0.25f}), T.fabric);      // vaso
    draw(M.cyl, part(Vs, {0, 0.36f, 0}, {0.4f, 0.05f, 0.4f}), fabricMat({0.75f, 0.4f, 0.25f}), T.fabric);     // borda
    draw(M.cyl, part(Vs, {0, 0.385f, 0}, {0.34f, 0.02f, 0.34f}), fabricMat({0.25f, 0.15f, 0.1f}), T.fabric);  // terra
    draw(M.cyl, part(Vs, {0, 0.58f, 0}, {0.03f, 0.4f, 0.03f}), fabricMat({0.2f, 0.5f, 0.2f}), T.fabric);      // caule
    for (int k = 0; k < 6; k++) {
        mat4 lf = glm::rotate(Vs, -glm::radians(60.0f * k), vec3(0, 1, 0));                                   // folhas em circulo
        draw(M.sph, part(lf, {0.13f, 0.62f + 0.04f * k, 0}, {0.2f, 0.07f, 0.12f}), fabricMat({0.2f, 0.6f, 0.25f}), T.fabric);
    }

    // --- cesto de roupa junto a parede oeste (ripas em circulo, fundo e roupa)
    mat4 Ct = glm::translate(I, vec3(-3.5f, 0, -1.0f));
    draw(M.cyl, part(Ct, {0, 0.02f, 0}, {0.5f, 0.03f, 0.5f}), fabricMat({0.8f, 0.65f, 0.4f}), T.fabric);
    for (int k = 0; k < 16; k++) {
        mat4 sl = glm::rotate(Ct, glm::radians(22.5f * k), vec3(0, 1, 0));
        draw(M.cube, part(sl, {0.25f, 0.24f, 0}, {0.03f, 0.44f, 0.1f}), fabricMat({0.8f, 0.65f, 0.4f}), T.fabric);      // ripa
        draw(M.cube, part(sl, {0.255f, 0.1f, 0}, {0.04f, 0.03f, 0.11f}), fabricMat({0.6f, 0.45f, 0.25f}), T.fabric);    // cinta baixa
        draw(M.cube, part(sl, {0.255f, 0.42f, 0}, {0.04f, 0.03f, 0.11f}), fabricMat({0.6f, 0.45f, 0.25f}), T.fabric);   // cinta alta
    }
    draw(M.sph, part(Ct, {0, 0.36f, 0}, {0.34f, 0.2f, 0.34f}), fabricMat({0.55f, 0.65f, 1.0f}), T.fabric, {2, 2});      // roupa

    // --- itens: moedas giratorias (rotacao animada) e flutuantes
    for (int i = 0; i < (int)G.items.size(); i++) {
        if (G.items[i].got || !available(G.items[i])) continue;
        mat4 m = glm::translate(I, itemPos(i, t));
        m = glm::rotate(m, (float)t * 1.5f, vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(90.0f), vec3(1, 0, 0));
        draw(M.cyl, glm::scale(m, vec3(0.2f, 0.03f, 0.2f)), M_GOLD, T.metal);
    }

    // --- vidro da janela: por ultimo, com transparencia (blending) e sem escrever profundidade
    
    if (gShadowPass) return;  
    glEnable(GL_BLEND);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1f(U("uAlpha"), 0.25f);
    draw(M.cube, part(W, {0.02f, 0, 0}, {0.02f, 1.14f, 1.24f}), M_GLASS, T.white);
    glUniform1f(U("uAlpha"), 1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
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
    gSkyProg = buildSkyProgram();
    gSkyTex = makeSkybox();
    initShadow();
    Meshes M{makeCube(), makeCylinder(), makeSphere()};
    Textures T = makeTextures();
    hudInit();

    G.items = {
        {{1.0f, 0.1f, -3.7f}, 1, false},    // dentro do armario
        {{-2.05f, 0.66f, -3.2f}, 2, false}, // dentro da gaveta
        {{2.45f, 0.1f, -2.0f}, 0, false},   // embaixo da cama
        {{-3.8f, 1.28f, 0.8f}, 0, false},   // na estante
        {{3.6f, 0.1f, 3.6f}, 0, false},     // no canto escuro
        {{1.0f, 2.32f, -3.7f}, 0, false},   // em cima do armario
        {{1.95f, 0.36f, -2.95f}, 4, false}, // dentro do criado-mudo
        {{3.65f, 0.22f, 1.8f}, 5, false},   // dentro do bau
    };
    G.start = glfwGetTime();
    double last = G.start;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime(), dt = now - last; last = now;
        glfwPollEvents();

        // ---- jogo
        if (G.started && !G.ended) {
            if (TIME_LIMIT - (now - G.start) <= 0) endGame("Tempo esgotado");
            else if (foundCount() == (int)G.items.size()) endGame("Voce venceu!");
        }

        // ---- movimento WASD com colisao (eixos separados)
        if (G.started && !G.ended) {
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

        // ---- ventilador de teto
        if (G.fanOn) G.fanAngle = std::fmod(G.fanAngle + 540.0f * (float)dt, 360.0f);

        // ---- interacao
        Target tgt = (G.ended || !G.started) ? Target() : pick(now);
        if ((tgt.kind == 0 || tgt.kind == 4) && pressE) interact(tgt);   // E pega moeda e chave
        if ((tgt.kind == 1 || tgt.kind == 2 || tgt.kind == 7 || tgt.kind == 8) && pressF) interact(tgt);  // F abre e fecha// F abre e fecha
        if ((tgt.kind == 3 || tgt.kind == 5) && pressG) interact(tgt);   // G liga e desliga
        pressE = false;
        pressF = false;
        pressG = false;

        // ---- animacoes (interpolacao suave ate o alvo)
        auto approach = [&](float& v, float target, float speed) {
            float step = speed * (float)dt, d = target - v;
            v += glm::clamp(d, -step, step);
        };
        approach(G.closet, G.closetT, 2.5f);
        approach(G.drawer, G.drawerT, 0.8f);
        approach(G.nightD, G.nightDT, 0.8f);
        approach(G.chest, G.chestT, 1.5f);

        // ---- render
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        if (h == 0) h = 1;
        // ---- passada de sombra: a cena vista pela lua, gravando so a profundidade
        const vec3 center(0.0f, 1.5f, 0.0f);
        mat4 lightView = glm::lookAt(center - SUN_DIR * 10.0f, center, vec3(0, 1, 0));
        mat4 lightProj = glm::ortho(-6.5f, 6.5f, -6.5f, 6.5f, 1.0f, 22.0f);
        mat4 lightSpace = lightProj * lightView;
        glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
        glBindFramebuffer(GL_FRAMEBUFFER, gShadowFbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        {
            GLuint saved = gProg; gProg = gDepthProg; glUseProgram(gProg);   // draw() passa a usar o shader de profundidade
            glUniformMatrix4fv(U("uLightSpace"), 1, GL_FALSE, glm::value_ptr(lightSpace));
            gShadowPass = true; drawScene(M, T, now); gShadowPass = false;
            gProg = saved;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
        glUniform3fv(U("uSunDir"), 1, glm::value_ptr(SUN_DIR));
        glUniform3f(U("uSunColor"), 0.55f, 0.62f, 0.90f);
        glUniformMatrix4fv(U("uLightSpace"), 1, GL_FALSE, glm::value_ptr(lightSpace));
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gShadowTex); glActiveTexture(GL_TEXTURE0);
        glUniform1i(U("uShadowMap"), 1);
        glUniform1i(U("uFlashOn"), G.flashOn ? 1 : 0);
        glUniform3fv(U("uFlashPos"), 1, glm::value_ptr(camPos));
        glUniform3fv(U("uFlashDir"), 1, glm::value_ptr(camFront()));
        glUniform3fv(U("uLampPos"), 1, glm::value_ptr(LAMP_LIGHT_POS));
        glUniform3f(U("uLampColor"), 2.0f, 1.7f, 1.2f);
        glUniform1i(U("uLampOn"), G.lampOn ? 1 : 0);
        glUniform1f(U("uAlpha"), 1.0f);

        // ---- skybox: desenhado primeiro, sem profundidade (so aparece pela janela)
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glUseProgram(gSkyProg);
        glUniformMatrix4fv(glGetUniformLocation(gSkyProg, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(gSkyProg, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glBindTexture(GL_TEXTURE_CUBE_MAP, gSkyTex);
        glBindVertexArray(M.cube.vao);
        glDrawElements(GL_TRIANGLES, M.cube.count, GL_UNSIGNED_INT, 0);
        glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
        glUseProgram(gProg);
        drawScene(M, T, now);

        // ---- HUD 2D por cima da cena
        drawHud(w, h, now, tgt);
        hudFlush(w, h);
        glfwSwapBuffers(win);
    }
    glfwTerminate();
    return 0;

}
