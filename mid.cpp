// main.cpp
// Simple steam / chimney particle prototype using geometry shader billboarding.
// Single-file prototype (requires GLFW + GLAD). No external textures.
//
// Compile (example):
// g++ -Wall -std=c++17 main.cpp glad.c -Iinclude -o steam.exe -lglfw3 -lopengl32 -lgdi32
//
// Notes:
// - Keep glad.c and glad headers available (or adapt loader).
// - This intentionally avoids GLM; particle math is simple and done inline.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <random>
#include <iostream>

struct Particle {
    float x,y,z;
    float vx,vy,vz;
    float life;   // remaining life (seconds)
    float size;   // visual size in NDC units
};

const int MAX_PARTICLES = 1000;
std::vector<Particle> particles;
std::mt19937 rng(12345);
std::uniform_real_distribution<float> urf(-1.0f, 1.0f);

// simple timer
double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(high_resolution_clock::now().time_since_epoch()).count();
}

// ---------- shaders (embedded) ----------
const char* vertSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aSize;

out float vSize;
out vec3 vPos;

void main() {
    // Pass through position (we treat positions as NDC-like coordinates for prototype)
    vPos = aPos;
    vSize = aSize;
    // Dummy gl_Position for point primitive pipeline (actual quads emitted in GS)
    gl_Position = vec4(aPos, 1.0);
}
)";

const char* geomSrc = R"(#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

in float vSize[];
in vec3 vPos[];

out vec2 texCoord;
out float vLife; // not used here, but could pass alpha per-vertex

uniform vec3 cameraRight; // direction in world/NDC space
uniform vec3 cameraUp;

void main() {
    vec3 center = vPos[0];
    float s = vSize[0];

    vec3 right = normalize(cameraRight) * s;
    vec3 up    = normalize(cameraUp) * s;

    // create quad (triangle strip)
    // Lower-left
    texCoord = vec2(0.0, 0.0);
    gl_Position = vec4(center - right - up, 1.0);
    EmitVertex();

    // Upper-left
    texCoord = vec2(0.0, 1.0);
    gl_Position = vec4(center - right + up, 1.0);
    EmitVertex();

    // Lower-right
    texCoord = vec2(1.0, 0.0);
    gl_Position = vec4(center + right - up, 1.0);
    EmitVertex();

    // Upper-right
    texCoord = vec2(1.0, 1.0);
    gl_Position = vec4(center + right + up, 1.0);
    EmitVertex();

    EndPrimitive();
}
)";

const char* fragSrc = R"(#version 330 core
in vec2 texCoord;
out vec4 FragColor;

uniform float globalAlpha;

void main() {
    // create a soft circular particle using distance from center
    vec2 c = texCoord - vec2(0.5);
    float dist = length(c);
    float alpha = smoothstep(0.5, 0.0, dist); // strong falloff
    // tweak alpha curve for "steam" look
    alpha = pow(alpha, 0.6);
    alpha *= globalAlpha;

    // pale bluish-white steam
    vec3 color = vec3(0.9, 0.95, 1.0);

    if (alpha < 0.01) discard;
    FragColor = vec4(color, alpha);
}
)";

// ---------- helper: compile shader ----------
GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024]; glGetShaderInfoLog(s, 1024, nullptr, buf);
        std::cerr << "Shader compile error: " << buf << std::endl;
    }
    return s;
}

GLuint linkProgram(GLuint vs, GLuint gs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    if (gs) glAttachShader(p, gs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024]; glGetProgramInfoLog(p, 1024, nullptr, buf);
        std::cerr << "Program link error: " << buf << std::endl;
    }
    return p;
}

// ---------- main ----------
int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    GLFWwindow* win = glfwCreateWindow(800, 600, "Steam prototype", nullptr, nullptr);
    if (!win) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return -1;
    }

    // GL state
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // depth off for simplicity (steam blends nicely without depth)
    glDisable(GL_DEPTH_TEST);

    // compile shaders
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint gs = compileShader(GL_GEOMETRY_SHADER, geomSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    GLuint program = linkProgram(vs, gs, fs);
    glDeleteShader(vs); glDeleteShader(gs); glDeleteShader(fs);

    // particle buffers: we'll stream positions and sizes
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // Reserve space: MAX_PARTICLES * (vec3 + float) = 4 floats per particle
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 4 * sizeof(float), nullptr, GL_STREAM_DRAW);
    // position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // size (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);

    // particle data container
    particles.reserve(MAX_PARTICLES);

    auto spawnParticle = [&](float x, float y, float z) {
        if ((int)particles.size() >= MAX_PARTICLES) return;
        Particle p;
        p.x = x + urf(rng) * 0.02f;
        p.y = y + urf(rng) * 0.01f;
        p.z = z + urf(rng) * 0.01f;
        // upward velocity with a little spread
        p.vx = urf(rng) * 0.02f;
        p.vy = 0.4f + (urf(rng) * 0.15f); // upward speed
        p.vz = urf(rng) * 0.02f;
        p.life = 1.8f + (urf(rng) * 0.6f); // seconds
        p.size = 0.03f + (0.03f * ((urf(rng)+1.0f)/2.0f)); // NDC-ish
        particles.push_back(p);
    };

    double lastTime = nowSeconds();
    double accumulator = 0.0;
    float spawnRate = 150.0f; // particles per second (tweak)
    double spawnAccumulator = 0.0;

    // emitter position (in NDC-like coords): chimney at bottom-center
    const float emitterX = 0.0f;
    const float emitterY = -0.6f;
    const float emitterZ = 0.0f;

    while (!glfwWindowShouldClose(win)) {
        double t = nowSeconds();
        double dt = t - lastTime;
        lastTime = t;
        if (dt > 0.05) dt = 0.05; // clamp large deltas

        // spawn according to rate
        spawnAccumulator += dt * spawnRate;
        while (spawnAccumulator >= 1.0) {
            spawnParticle(emitterX, emitterY, emitterZ);
            spawnAccumulator -= 1.0;
        }

        // update particles
        for (int i = (int)particles.size()-1; i >= 0; --i) {
            Particle &p = particles[i];
            // simple wind noise (sin-based)
            float timeFactor = (float)t;
            float windX = 0.05f * std::sin(timeFactor * 1.3f + p.x*10.0f);
            float windZ = 0.03f * std::cos(timeFactor * 1.7f + p.z*8.0f);

            p.vx += windX * (float)dt;
            p.vz += windZ * (float)dt;
            // gentle slowdown (like air resistance)
            p.vx *= 0.995f;
            p.vy *= 0.999f;
            p.vz *= 0.995f;

            p.x += p.vx * (float)dt;
            p.y += p.vy * (float)dt;
            p.z += p.vz * (float)dt;

            // enlarge slightly as it rises
            p.size *= 1.0f + 0.25f * (float)dt;

            p.life -= (float)dt;
            if (p.life <= 0.0f || p.y > 1.2f) {
                // swap-pop remove
                particles[i] = particles.back();
                particles.pop_back();
            }
        }

        // upload particle buffer (positions + size)
        std::vector<float> buffer;
        buffer.reserve(particles.size() * 4);
        for (auto &p : particles) {
            buffer.push_back(p.x);
            buffer.push_back(p.y);
            buffer.push_back(p.z);
            buffer.push_back(p.size);
        }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // orphan and refill
        glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 4 * sizeof(float), nullptr, GL_STREAM_DRAW);
        if (!buffer.empty())
            glBufferSubData(GL_ARRAY_BUFFER, 0, buffer.size() * sizeof(float), buffer.data());

        // render
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);

        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);

        // cameraRight / cameraUp in NDC-like coordinates (simple orthographic view)
        // For a straightforward camera looking down -Z with up=(0,1,0) and right=(1,0,0):
        GLint locR = glGetUniformLocation(program, "cameraRight");
        GLint locU = glGetUniformLocation(program, "cameraUp");
        glUniform3f(locR, 1.0f * 0.02f * (float)h / (float)w, 0.0f, 0.0f); // scale right by aspect to keep quad shapes
        glUniform3f(locU, 0.0f, 1.0f * 0.02f, 0.0f);

        // global alpha control (could be based on time of day)
        float globalAlpha = 0.9f;
        GLint locA = glGetUniformLocation(program, "globalAlpha");
        glUniform1f(locA, globalAlpha);

        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, (GLsizei)particles.size());
        glBindVertexArray(0);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

