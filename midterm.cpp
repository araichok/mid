// main.cpp — Steam Chimney with Simple House Model
// Make sure to link: OpenGL, GLFW, and GLAD

#include </Users/ayaulym/Downloads/Files-2/Polygon/Triangle/External/include/glad/glad.h>
#include </opt/homebrew/Cellar/glfw/3.4/include/GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

#define MAX_PARTICLES 4000

// ---------- BASIC SHADERS FOR CUBE ----------
static const char* cubeVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 inPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(inPos, 1.0);
}
)GLSL";

static const char* cubeFS = R"GLSL(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)GLSL";

// ---------- PARTICLE SHADERS (same as before) ----------
static const char* vertexShaderSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inLife;
layout(location = 2) in float inSize;
out float vLife;
out float vSize;
void main() {
    gl_Position = vec4(inPosition, 1.0);
    vLife = inLife;
    vSize = inSize;
}
)GLSL";

static const char* geometryShaderSrc = R"GLSL(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in float vLife[];
in float vSize[];
out vec2 gUV;
out float fLife;
uniform mat4 uV;
uniform mat4 uP;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
void main() {
    vec3 pos = gl_in[0].gl_Position.xyz;
    float s = vSize[0];
    float life = vLife[0];
    vec3 right = normalize(uCameraRight) * s;
    vec3 up = normalize(uCameraUp) * s;
    vec4 wpos;
    gUV = vec2(0.0, 0.0); fLife = life; wpos = vec4(pos - right - up, 1.0); gl_Position = uP * uV * wpos; EmitVertex();
    gUV = vec2(1.0, 0.0); fLife = life; wpos = vec4(pos + right - up, 1.0); gl_Position = uP * uV * wpos; EmitVertex();
    gUV = vec2(0.0, 1.0); fLife = life; wpos = vec4(pos - right + up, 1.0); gl_Position = uP * uV * wpos; EmitVertex();
    gUV = vec2(1.0, 1.0); fLife = life; wpos = vec4(pos + right + up, 1.0); gl_Position = uP * uV * wpos; EmitVertex();
    EndPrimitive();
}
)GLSL";

static const char* fragmentShaderSrc = R"GLSL(
#version 330 core
in vec2 gUV;
in float fLife;
out vec4 FragColor;
uniform vec3 uColor0;
uniform vec3 uColor1;
void main() {
    vec2 c = gUV - vec2(0.5);
    float dist = length(c) / 0.5;
    float alpha = exp(-dist * dist * 4.0);
    alpha *= clamp(fLife, 0.0, 1.0);
    vec3 col = mix(uColor1, uColor0, fLife);
    FragColor = vec4(col, alpha);
    if (FragColor.a < 0.02) discard;
}
)GLSL";

// ---------- UTILS ----------
static GLuint compile(GLenum t, const char* s) {
    GLuint id = glCreateShader(t);
    glShaderSource(id, 1, &s, nullptr);
    glCompileShader(id);
    GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[512]; glGetShaderInfoLog(id, 512, nullptr, info);
        std::cerr << "Shader error:\n" << info << std::endl;
    }
    return id;
}

static GLuint makeProg(const char* vs, const char* fs, const char* gs = nullptr) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    GLuint g = 0;
    if (gs) { g = compile(GL_GEOMETRY_SHADER, gs); glAttachShader(p, g); }
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    if (g) glDeleteShader(g);
    return p;
}

struct Particle {
    glm::vec3 pos, vel;
    float life, lifetime, size;
};

// ---------- MAIN ----------
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* w = glfwCreateWindow(1280, 720, "Steam Chimney", 0, 0);
    glfwMakeContextCurrent(w);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint steamProg = makeProg(vertexShaderSrc, fragmentShaderSrc, geometryShaderSrc);
    GLuint cubeProg = makeProg(cubeVS, cubeFS);

    // ----- Cube data for house base + chimney -----
    float cubeVerts[] = {
        -0.5f, 0.0f, -0.5f,   0.5f, 0.0f, -0.5f,   0.5f, 1.0f, -0.5f,  -0.5f, 1.0f, -0.5f,
        -0.5f, 0.0f,  0.5f,   0.5f, 0.0f,  0.5f,   0.5f, 1.0f,  0.5f,  -0.5f, 1.0f,  0.5f,
    };
    unsigned int cubeIdx[] = {
        0,1,2,2,3,0, 4,5,6,6,7,4, 0,4,7,7,3,0, 1,5,6,6,2,1, 3,2,6,6,7,3, 0,1,5,5,4,0
    };

    GLuint cubeVAO, cubeVBO, cubeEBO;
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glGenBuffers(1, &cubeEBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIdx), cubeIdx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
    glBindVertexArray(0);

    // ----- Particles -----
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    std::vector<Particle> parts;
    glm::vec3 emitter(0.0f, 1.2f, 0.0f); // on top of chimney
    std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<float> rnd(0, 1);

    float last = glfwGetTime();
    while (!glfwWindowShouldClose(w)) {
        float t = glfwGetTime(), dt = t - last; last = t;
        glfwPollEvents();

        // update particles
        for (auto& p : parts) {
            p.vel += glm::vec3(0, 0.5, 0) * dt;
            p.pos += p.vel * dt;
            p.life -= dt;
        }
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                                   [](const Particle& p){return p.life<=0;}),
                    parts.end());

        for (int i=0;i<8;i++) {
            Particle p;
            p.pos = emitter + glm::vec3((rnd(rng)-0.5f)*0.2f,0,(rnd(rng)-0.5f)*0.2f);
            p.vel = glm::vec3(0,(rnd(rng)*1.5f)+1.0f,0);
            p.lifetime = 2.0f;
            p.life = p.lifetime;
            p.size = 0.25f;
            parts.push_back(p);
        }

        int W,H; glfwGetFramebufferSize(w,&W,&H);
        glViewport(0,0,W,H);
        glClearColor(0.5,0.7,0.95,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        glm::vec3 cam(0,2,5);
        glm::mat4 V = glm::lookAt(cam, glm::vec3(0,1,0), glm::vec3(0,1,0));
        glm::mat4 P = glm::perspective(glm::radians(45.0f), (float)W/H, 0.1f, 100.0f);

        // ---- Draw house base ----
        glUseProgram(cubeProg);
        glm::mat4 M = glm::translate(glm::mat4(1), glm::vec3(0,-0.5,0));
        M = glm::scale(M, glm::vec3(2,1,2));
        glm::mat4 MVP = P*V*M;
        glUniformMatrix4fv(glGetUniformLocation(cubeProg,"uMVP"),1,GL_FALSE,glm::value_ptr(MVP));
        glUniform3f(glGetUniformLocation(cubeProg,"uColor"),0.6f,0.4f,0.3f);
        glBindVertexArray(cubeVAO);
        glDrawElements(GL_TRIANGLES,36,GL_UNSIGNED_INT,0);

        // ---- Draw chimney ----
        M = glm::translate(glm::mat4(1), glm::vec3(0,0.5,0));
        M = glm::scale(M, glm::vec3(0.3,1.2,0.3));
        MVP = P*V*M;
        glUniformMatrix4fv(glGetUniformLocation(cubeProg,"uMVP"),1,GL_FALSE,glm::value_ptr(MVP));
        glUniform3f(glGetUniformLocation(cubeProg,"uColor"),0.3f,0.3f,0.3f);
        glDrawElements(GL_TRIANGLES,36,GL_UNSIGNED_INT,0);

        // ---- Draw particles ----
        glUseProgram(steamProg);
        glUniformMatrix4fv(glGetUniformLocation(steamProg,"uV"),1,GL_FALSE,glm::value_ptr(V));
        glUniformMatrix4fv(glGetUniformLocation(steamProg,"uP"),1,GL_FALSE,glm::value_ptr(P));
        glUniform3f(glGetUniformLocation(steamProg,"uColor0"),0.95f,0.95f,0.95f);
        glUniform3f(glGetUniformLocation(steamProg,"uColor1"),0.7f,0.7f,0.8f);
        glm::vec3 right(V[0][0],V[1][0],V[2][0]);
        glm::vec3 up(V[0][1],V[1][1],V[2][1]);
        glUniform3fv(glGetUniformLocation(steamProg,"uCameraRight"),1,glm::value_ptr(right));
        glUniform3fv(glGetUniformLocation(steamProg,"uCameraUp"),1,glm::value_ptr(up));

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        const GLsizei stride = 5*sizeof(float);
        glBufferData(GL_ARRAY_BUFFER,MAX_PARTICLES*stride,nullptr,GL_STREAM_DRAW);
        if(!parts.empty()){
            std::vector<float> buf;
            buf.reserve(parts.size()*5);
            for(auto&p:parts){
                buf.push_back(p.pos.x);buf.push_back(p.pos.y);
                buf.push_back(p.pos.z);
                buf.push_back(p.life/p.lifetime);
                buf.push_back(p.size);
            }
            glBufferSubData(GL_ARRAY_BUFFER,0,buf.size()*sizeof(float),buf.data());
        }
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,stride,(void*)(4*sizeof(float)));
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_POINTS,0,parts.size());
        glDepthMask(GL_TRUE);

        glfwSwapBuffers(w);
    }
    glfwTerminate();
    return 0;
}
