// Doom/Quake-like FPS OBJ project with SFML Audio for music.
// Enemy rendering is scaled down in size.
// Simulates a loading bar at startup for 7 seconds.
// Multifile tinyobjloader setup: requires tiny_obj_loader.h, tiny_obj_loader.cc
//This game is open source, so use and edit as you see fit, but give me credit as the author
//of this program. Copyright(2025) Joshua Conner.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <SFML/Audio.hpp>
#include <cmath>
#include <vector>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "tiny_obj_loader.h"

extern "C" int run_game();

// ----- MODEL STRUCTURE -----
struct Model {
    GLuint VAO, VBO, EBO;
    GLuint texture;
    std::vector<float> vertices; // layout: pos(3), normal(3), uv(2)
    std::vector<unsigned int> indices;
    size_t indexCount = 0;
};

// ----- OBJ MODEL LOADER (tinyobjloader) -----
bool loadModelOBJ(const std::string& objPath, const std::string& texPath, Model& model) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str())) {
        std::cout << "OBJ load failed: " << objPath << "\n";
        return false;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            float vx = attrib.vertices[3 * idx.vertex_index + 0];
            float vy = attrib.vertices[3 * idx.vertex_index + 1];
            float vz = attrib.vertices[3 * idx.vertex_index + 2];
            float nx = idx.normal_index >= 0 ? attrib.normals[3 * idx.normal_index + 0] : 0;
            float ny = idx.normal_index >= 0 ? attrib.normals[3 * idx.normal_index + 1] : 0;
            float nz = idx.normal_index >= 0 ? attrib.normals[3 * idx.normal_index + 2] : 0;
            float u = idx.texcoord_index >= 0 ? attrib.texcoords[2 * idx.texcoord_index + 0] : 0;
            float v = idx.texcoord_index >= 0 ? attrib.texcoords[2 * idx.texcoord_index + 1] : 0;
            vertices.insert(vertices.end(), {vx, vy, vz, nx, ny, nz, u, v});
            indices.push_back(indices.size());
        }
    }
    model.vertices = vertices;
    model.indices = indices;
    model.indexCount = indices.size();

    // OpenGL Buffer setup
    glGenVertexArrays(1, &model.VAO);
    glBindVertexArray(model.VAO);
    glGenBuffers(1, &model.VBO);
    glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &model.EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); // uv
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Texture loading
    int w, h, c;
    unsigned char* data = stbi_load(texPath.c_str(), &w, &h, &c, 0);
    if (!data) {
        std::cout << "Texture load failed: " << texPath << "\n";
        return false;
    }
    glGenTextures(1, &model.texture);
    glBindTexture(GL_TEXTURE_2D, model.texture);
    GLenum fmt = (c == 3 ? GL_RGB : GL_RGBA);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return true;
}

// ----- DATA STRUCTURES -----
struct Vec3 { float x, y, z; };
struct Enemy { Vec3 pos; bool alive; float cooldown; };

float cameraX = 2, cameraY = 1.0f, cameraZ = 2;
float yaw = 3.14f, pitch = 0.0f;
int health = 100, ammo = 10, score = 0;
std::vector<Enemy> enemies;
std::vector<Vec3> projectiles;

// ----- MAP -----
const int MAP_W = 8, MAP_H = 8;
int map[MAP_W * MAP_H] = {
    1,1,1,1,1,1,1,1,
    1,0,0,0,0,0,2,1,
    1,0,1,0,1,0,0,1,
    1,0,1,0,1,0,0,1,
    1,0,0,0,0,1,0,1,
    1,0,1,1,0,0,0,1,
    1,2,0,0,0,1,0,1,
    1,1,1,1,1,1,1,1
};

// ----- UTILITY FUNCTIONS -----
void mat4_identity(float* m) { for (int i = 0; i < 16; i++) m[i] = 0; m[0] = m[5] = m[10] = m[15] = 1; }
void mat4_translate(float* m, float x, float y, float z) { mat4_identity(m); m[12] = x; m[13] = y; m[14] = z; }
void mat4_scale(float* m, float s) { mat4_identity(m); m[0] = s; m[5] = s; m[10] = s; }
void mat4_multiply(float* result, const float* a, const float* b) {
    for(int i=0;i<16;i++) result[i]=0;
    for(int row=0;row<4;row++)
        for(int col=0;col<4;col++)
            for(int k=0;k<4;k++)
                result[row*4+col] += a[row*4+k]*b[k*4+col];
}
void mat4_perspective(float* m, float fov, float aspect, float near, float far) {
    mat4_identity(m); float tanHalfFov = tan(fov / 2.f);
    m[0] = 1.f / (aspect * tanHalfFov); m[5] = 1.f / tanHalfFov;
    m[10] = -(far + near) / (far - near); m[11] = -1;
    m[14] = -(2.f * far * near) / (far - near); m[15] = 0;
}
void mat4_lookat(float* m, float px, float py, float pz, float tx, float ty, float tz) {
    float fx = tx - px, fy = ty - py, fz = tz - pz;
    float fl = sqrt(fx * fx + fy * fy + fz * fz); fx /= fl; fy /= fl; fz /= fl;
    float upx = 0, upy = 1, upz = 0;
    float sx = upy * fz - upz * fy, sy = upz * fx - upx * fz, sz = upx * fy - upy * fx;
    float sl = sqrt(sx * sx + sy * sy + sz * sz); sx /= sl; sy /= sl; sz /= sl;
    float ux = fy * sz - fz * sy, uy = fz * sx - fx * sz, uz = fx * sy - fy * sx;
    mat4_identity(m); m[0] = sx; m[4] = sy; m[8] = sz;
    m[1] = ux; m[5] = uy; m[9] = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[12] = -(sx * px + sy * py + sz * pz);
    m[13] = -(ux * px + uy * py + uz * pz);
    m[14] = fx * px + fy * py + fz * pz;
}
bool aabb_collision(Vec3 a, Vec3 b, float s = 0.5f) {
    return std::abs(a.x - b.x) < s && std::abs(a.y - b.y) < s && std::abs(a.z - b.z) < s;
}
void draw_text(int health, int ammo, int score) {
    std::cout << "\rHealth: " << health << " Ammo: " << ammo << " Score: " << score << "   " << std::flush;
}

// ----- SHADERS (with lighting) -----
const char* vertexShaderSource = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;
layout(location=2) in vec3 aNormal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;
out vec2 TexCoord;
out vec3 FragPos;
out vec3 Normal;
void main() {
    gl_Position=proj*view*model*vec4(aPos,1.0);
    TexCoord = aTexCoord;
    FragPos = vec3(model * vec4(aPos,1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
}
)";
const char* fragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec3 color;
uniform vec3 lightPos;
uniform vec3 viewPos;
void main() {
    vec3 ambient = 0.25 * color;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * color;
    vec3 result = ambient + diffuse;
    FragColor = texture(texture1, TexCoord) * vec4(result,1.0);
}
)";

// ----- MOUSE INPUT -----
bool firstMouse = true; double lastX = 400, lastY = 300;
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    float sensitivity = 0.002f;
    float dx = xpos - lastX, dy = lastY - ypos;
    yaw += dx * sensitivity;
    pitch += dy * sensitivity;
    if (pitch > 1.5f)
        pitch = 1.5f;
    if (pitch < -1.5f)
        pitch = -1.5f;
    lastX = xpos;
    lastY = ypos;
}

// ----- LOADING BAR -----
void simulateLoadingBar(int seconds) {
    const int barWidth = 50;
    std::cout << "Loading: ";
    for (int i = 0; i <= barWidth; ++i) {
        int progress = i * 100 / barWidth;
        std::cout << "\rLoading: [";
        for (int j = 0; j < barWidth; ++j)
            std::cout << (j < i ? '=' : ' ');
        std::cout << "] " << progress << "%";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(seconds * 1000 / barWidth));
    }
    std::cout << std::endl;
}

// ----- SHOOTING -----
void shoot() {
    if (ammo <= 0) return;
    float dirX = sin(yaw) * cos(pitch), dirY = sin(pitch), dirZ = cos(yaw) * cos(pitch);
    projectiles.push_back({ cameraX + dirX, cameraY + dirY, cameraZ + dirZ });
    ammo--;
}

// ----- MAIN -----
int main() {
    // Simulate loading bar for 7 seconds
    simulateLoadingBar(7);

    // Setup OpenGL/GLFW
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "DoomQuakeFPS_OBJ", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);

    // ---- MUSIC ----
    sf::Music music;
    if (!music.openFromFile("Momentum.mp3")) {
        std::cout << "Could not open Momentum.mp3\n";
    } else {
        music.setLoop(true);
        music.play();
    }

    // Compile shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL); glCompileShader(vertexShader);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL); glCompileShader(fragmentShader);
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader); glAttachShader(shaderProgram, fragmentShader); glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader); glDeleteShader(fragmentShader);

    // Load models (OBJ + PNG)
    Model wallModel, enemyModel, gunModel;
    if (!loadModelOBJ("wall.obj", "wall.png", wallModel)) return -1;
    if (!loadModelOBJ("enemy.obj", "enemy.png", enemyModel)) return -1;
    if (!loadModelOBJ("gun.obj", "gun.png", gunModel)) return -1;

    // Spawn enemies
    for (int y = 0; y < MAP_H; ++y) for (int x = 0; x < MAP_W; ++x) {
        if (map[y * MAP_W + x] == 2) enemies.push_back({ Vec3{ (float)x,1.f,(float)y },true,0 });
    }

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        // Controls
        float speed = 0.10f;
        float dirX = sin(yaw) * cos(pitch), dirY = sin(pitch), dirZ = cos(yaw) * cos(pitch);
        float strafeX = cos(yaw), strafeZ = -sin(yaw);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            float nx = cameraX + dirX * speed, ny = cameraY + dirY * speed, nz = cameraZ + dirZ * speed;
            int mx = int(nx), mz = int(nz);
            if (map[mz * MAP_W + mx] == 0) { cameraX = nx; cameraY = ny; cameraZ = nz; }
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            float nx = cameraX - dirX * speed, ny = cameraY - dirY * speed, nz = cameraZ - dirZ * speed;
            int mx = int(nx), mz = int(nz);
            if (map[mz * MAP_W + mx] == 0) { cameraX = nx; cameraY = ny; cameraZ = nz; }
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            float nx = cameraX - strafeX * speed, nz = cameraZ - strafeZ * speed;
            int mx = int(nx), mz = int(nz);
            if (map[mz * MAP_W + mx] == 0) { cameraX = nx; cameraZ = nz; }
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            float nx = cameraX + strafeX * speed, nz = cameraZ + strafeZ * speed;
            int mx = int(nx), mz = int(nz);
            if (map[mz * MAP_W + mx] == 0) { cameraX = nx; cameraZ = nz; }
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { shoot(); }

        // Exit: Press ESCAPE to close the window and exit the game loop
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            std::cout << "\nExiting game (ESC pressed)!\n";
            break;
        }

        // Matrices
        float model[16], view[16], proj[16];
        mat4_identity(model);
        mat4_lookat(view, cameraX, cameraY, cameraZ, cameraX + dirX, cameraY + dirY, cameraZ + dirZ);
        mat4_perspective(proj, 3.14f / 3, 800.0f / 600.0f, 0.1f, 100.0f);

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, view);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "proj"), 1, GL_FALSE, proj);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 4.0f, 4.0f, 4.0f);
        glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), cameraX, cameraY, cameraZ);

        // Draw map (walls)
        for (int y = 0; y < MAP_H; ++y) for (int x = 0; x < MAP_W; ++x) {
            if (map[y * MAP_W + x] == 1) {
                mat4_translate(model, (float)x, 0.5f, (float)y);
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, model);
                glBindTexture(GL_TEXTURE_2D, wallModel.texture);
                glUniform3f(glGetUniformLocation(shaderProgram, "color"), 1, 1, 1);
                glBindVertexArray(wallModel.VAO);
                glDrawElements(GL_TRIANGLES, wallModel.indexCount, GL_UNSIGNED_INT, 0);
            }
        }

        // Draw enemies (scaled down)
        for (auto& e : enemies) {
            if (!e.alive) continue;
            // Move toward player (simple AI)
            float dx = cameraX - e.pos.x, dz = cameraZ - e.pos.z;
            float d = sqrt(dx * dx + dz * dz);

            // ---- ENEMY ATTACKING DISABLED ----
            // if (d < 0.1f) {
            //     health -= 1;
            // }
            // ---- END DISABLE ----

            if (d < 5.0f) {
                e.pos.x += dx / d * 0.02f; e.pos.z += dz / d * 0.02f;
            }

            // --- SCALE ENEMY DOWN ---
            float transMat[16], scaleMat[16], modelMat[16];
            mat4_translate(transMat, e.pos.x, 1.0f, e.pos.z);
            mat4_scale(scaleMat, 0.5f); // Change 0.5f to smaller value for even smaller enemies
            mat4_multiply(modelMat, transMat, scaleMat); // model = trans * scale

            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, modelMat);
            glBindTexture(GL_TEXTURE_2D, enemyModel.texture);
            glUniform3f(glGetUniformLocation(shaderProgram, "color"), 1, 0.2f, 0.2f);
            glBindVertexArray(enemyModel.VAO);
            glDrawElements(GL_TRIANGLES, enemyModel.indexCount, GL_UNSIGNED_INT, 0);
        }

        // Update/draw projectiles
        for (size_t i = 0; i < projectiles.size(); ++i) {
            Vec3& p = projectiles[i];
            float dirX = sin(yaw) * cos(pitch), dirY = sin(pitch), dirZ = cos(yaw) * cos(pitch);
            p.x += dirX * 0.3f; p.y += dirY * 0.3f; p.z += dirZ * 0.3f;
            // Draw
            mat4_translate(model, p.x, p.y, p.z);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, model);
            glBindTexture(GL_TEXTURE_2D, gunModel.texture);
            glUniform3f(glGetUniformLocation(shaderProgram, "color"), 1, 1, 0.4f);
            glBindVertexArray(gunModel.VAO);
            glDrawElements(GL_TRIANGLES, gunModel.indexCount, GL_UNSIGNED_INT, 0);
            // Hit enemy?
            for (auto& e : enemies) {
                if (!e.alive) continue;
                if (aabb_collision(p, e.pos, 0.7f)) {
                    e.alive = false; score += 10;
                    projectiles.erase(projectiles.begin() + i); --i; break;
                }
            }
            // Remove if out of bounds
            if (p.x < -2 || p.x > MAP_W || p.z < -2 || p.z > MAP_H) {
                projectiles.erase(projectiles.begin() + i); --i; continue;
            }
        }

        // HUD
        draw_text(health, ammo, score);

        // Win/lose
        if (health <= 0) { std::cout << "\nGame Over!\n"; break; }
        bool allDead = true; for (auto& e : enemies) if (e.alive) allDead = false;
        if (allDead) { std::cout << "\nYou Win!\n"; break; }

        glfwSwapBuffers(window); glfwPollEvents();
    }

    // Cleanup
    glDeleteVertexArrays(1, &wallModel.VAO); glDeleteBuffers(1, &wallModel.VBO); glDeleteBuffers(1, &wallModel.EBO);
    glDeleteVertexArrays(1, &enemyModel.VAO); glDeleteBuffers(1, &enemyModel.VBO); glDeleteBuffers(1, &enemyModel.EBO);
    glDeleteVertexArrays(1, &gunModel.VAO); glDeleteBuffers(1, &gunModel.VBO); glDeleteBuffers(1, &gunModel.EBO);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window); glfwTerminate();
    return 0;
}
