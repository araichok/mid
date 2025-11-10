#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <iostream>
#include <cstdlib>
#include <ctime>
const char* cubeVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* cubeFS = R"(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main()
{
    FragColor = vec4(uColor, 1.0);
}
)";
const char* particleVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform float uTime;

out vec3 vWorldPos;
out float vAlpha;

void main()
{
    float lifetime = 4.0;
    float seed = aPos.x * 13.37 + aPos.z * 7.91;
    float age = mod(uTime + seed, lifetime);

    float riseSpeed = 0.35;
    float spread = 0.25;
    float factor = age / lifetime;

    // Базовая точка выхода дыма (из трубы)
    vec3 base = vec3(0.6, 1.4, 0.0);

    // Небольшое горизонтальное колебание (эффект ветра)
    float windX = sin(uTime * 0.7 + seed) * 0.05 * factor;
    float windZ = cos(uTime * 0.9 + seed) * 0.05 * factor;

    vec3 offset = vec3(aPos.x * (1.0 + factor * 1.5) + windX,
                       age * riseSpeed,
                       aPos.z * (1.0 + factor * 1.5) + windZ);

    vWorldPos = base + offset;
    vAlpha = 1.0 - pow(factor, 1.6); // более мягкое затухание
    gl_Position = vec4(vWorldPos, 1.0);
}
)";
const char* particleGS = R"(#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

in vec3 vWorldPos[];
in float vAlpha[];
out vec2 gTexCoord;
out float gAlpha;

uniform mat4 uView;
uniform mat4 uProj;

void main()
{
    vec3 center = vWorldPos[0];
    float alpha = vAlpha[0];

    vec3 right = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 up    = vec3(uView[0][1], uView[1][1], uView[2][1]);

    // Дым теперь меньше
    float size = 0.18 * alpha;

    vec3 p0 = center + (-right - up) * size;
    vec3 p1 = center + ( right - up) * size;
    vec3 p2 = center + (-right + up) * size;
    vec3 p3 = center + ( right + up) * size;

    gAlpha = alpha;

    gl_Position = uProj * uView * vec4(p0, 1.0);
    gTexCoord = vec2(0.0, 0.0);
    EmitVertex();

    gl_Position = uProj * uView * vec4(p1, 1.0);
    gTexCoord = vec2(1.0, 0.0);
    EmitVertex();

    gl_Position = uProj * uView * vec4(p2, 1.0);
    gTexCoord = vec2(0.0, 1.0);
    EmitVertex();

    gl_Position = uProj * uView * vec4(p3, 1.0);
    gTexCoord = vec2(1.0, 1.0);
    EmitVertex();

    EndPrimitive();
}
)";
const char* particleFS = R"(#version 330 core
in vec2 gTexCoord;
in float gAlpha;
out vec4 FragColor;

void main()
{
    vec2 uv = gTexCoord;
    float d = distance(uv, vec2(0.5));
    if (d > 0.5) discard;

    // Мягкие края и постепенное рассеивание
    float edge = smoothstep(0.5, 0.25, d);
    float alpha = gAlpha * edge * 0.8;

    // Цвет — мягкий серо-голубой дым
    vec3 color = mix(vec3(0.85, 0.88, 0.92), vec3(0.9, 0.9, 0.95), 1.0 - gAlpha);

    FragColor = vec4(color, alpha);
}
)";


GLuint compileShader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(sh, 1024, nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
    }
    return sh;
}

GLuint makeProgram(const char* vsSrc, const char* fsSrc, const char* gsSrc = nullptr)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint gs = 0;
    if (gsSrc)
        gs = compileShader(GL_GEOMETRY_SHADER, gsSrc);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    if (gsSrc) glAttachShader(prog, gs);

    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(prog, 1024, nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    if (gsSrc) glDeleteShader(gs);

    return prog;
}

float cubeVerts[] = {
    -0.5f,-0.5f,-0.5f,
     0.5f,-0.5f,-0.5f,
     0.5f, 0.5f,-0.5f,
    -0.5f, 0.5f,-0.5f,
    -0.5f,-0.5f, 0.5f,
     0.5f,-0.5f, 0.5f,
     0.5f, 0.5f, 0.5f,
    -0.5f, 0.5f, 0.5f
};

unsigned int cubeIdx[] = {
    0,1,2, 2,3,0,
    4,5,6, 6,7,4,
    0,4,7, 7,3,0,
    1,5,6, 6,2,1,
    3,2,6, 6,7,3,
    0,1,5, 5,4,0
};


int main()
{
    
    std::srand((unsigned)std::time(nullptr));

    if (!glfwInit())
    {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(800, 600, "Steam from Chimney - Geometry Shader", nullptr, nullptr);
    if (!win)
    {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(win);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to init GLAD\n";
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint cubeProg  = makeProgram(cubeVS, cubeFS);
    GLuint smokeProg = makeProgram(particleVS, particleFS, particleGS);

    GLuint cubeVAO, cubeVBO, cubeEBO;
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glGenBuffers(1, &cubeEBO);

    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIdx), cubeIdx, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    const int NUM_PARTICLES = 700;
    std::vector<glm::vec3> particles(NUM_PARTICLES);
    for (int i = 0; i < NUM_PARTICLES; ++i)
    {
        float rx = ((std::rand() % 100) / 100.0f - 0.5f) * 0.18f;
        float rz = ((std::rand() % 100) / 100.0f - 0.5f) * 0.18f;
        particles[i] = glm::vec3(rx, 0.0f, rz);
    }

    GLuint smokeVAO, smokeVBO;
    glGenVertexArrays(1, &smokeVAO);
    glGenBuffers(1, &smokeVBO);

    glBindVertexArray(smokeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, smokeVBO);
    glBufferData(GL_ARRAY_BUFFER, particles.size() * sizeof(glm::vec3), particles.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    float startTime = (float)glfwGetTime();
    while (!glfwWindowShouldClose(win))
    {
        float t = (float)glfwGetTime() - startTime;

        glClearColor(0.6f, 0.85f, 1.0f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 P = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
        glm::mat4 V = glm::lookAt(glm::vec3(4.0f, 3.0f, 6.0f),
                                  glm::vec3(0.0f, 0.5f, 0.0f),
                                  glm::vec3(0.0f, 1.0f, 0.0f));

        glUseProgram(cubeProg);
        GLint locMVP   = glGetUniformLocation(cubeProg, "uMVP");
        GLint locColor = glGetUniformLocation(cubeProg, "uColor");

        glBindVertexArray(cubeVAO);

        {
            glm::mat4 M = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 2.0f));
            glm::mat4 MVP = P * V * M;
            glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
            glUniform3f(locColor, 0.65f, 0.45f, 0.25f);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }
        {
            glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.75f, 0.0f));
            M = glm::scale(M, glm::vec3(2.2f, 0.45f, 2.2f));
            glm::mat4 MVP = P * V * M;
            glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
            glUniform3f(locColor, 0.7f, 0.15f, 0.15f);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        {
            glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(0.6f, 1.0f, 0.0f));
            M = glm::scale(M, glm::vec3(0.3f, 0.6f, 0.3f));
            glm::mat4 MVP = P * V * M;
            glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
            glUniform3f(locColor, 0.3f, 0.3f, 0.3f);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }
{
    glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
    M = glm::scale(M, glm::vec3(10.0f, 0.05f, 10.0f));
    glm::mat4 MVP = P * V * M;
    glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));

    glUniform3f(locColor, 0.3f, 0.7f, 0.3f); 

    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

        {
            glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.25f, 1.01f));
            M = glm::scale(M, glm::vec3(0.4f, 0.6f, 0.05f));
            glm::mat4 MVP = P * V * M;
            glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
            glUniform3f(locColor, 0.35f, 0.23f, 0.12f);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        auto drawWindow = [&](float x)
        {
            glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.2f, 1.01f));
            M = glm::scale(M, glm::vec3(0.3f, 0.3f, 0.05f));
            glm::mat4 MVP = P * V * M;
            glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
            glUniform3f(locColor, 0.55f, 0.8f, 1.0f);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        };
        drawWindow(-0.6f);
        drawWindow( 0.6f);

for (int i = 0; i < 8; ++i) {
    float angle = i * glm::two_pi<float>() / 8.0f;
    float radius = 2.8f + ((i % 2) ? 0.3f : -0.3f);
    float x = cos(angle) * radius;
    float z = sin(angle) * radius;
    glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(x, -0.3f, z));
    M = glm::scale(M, glm::vec3(0.4f, 0.3f, 0.4f));
    glm::mat4 MVP = P * V * M;
    glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
    glUniform3f(locColor, 0.25f, 0.55f, 0.25f);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}


        glBindVertexArray(0);
        glUseProgram(smokeProg);

        GLint locView  = glGetUniformLocation(smokeProg, "uView");
        GLint locProj  = glGetUniformLocation(smokeProg, "uProj");
        GLint locTime  = glGetUniformLocation(smokeProg, "uTime");

        glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(V));
        glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(P));
        glUniform1f(locTime, t);

        glBindVertexArray(smokeVAO);
        glDrawArrays(GL_POINTS, 0, NUM_PARTICLES);
        glBindVertexArray(0);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
