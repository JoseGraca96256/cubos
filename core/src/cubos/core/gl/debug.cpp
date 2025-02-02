#include <cubos/core/gl/debug.hpp>

#include <list>
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

using namespace cubos::core;
using namespace cubos::core::gl;

RenderDevice* Debug::renderDevice;
ConstantBuffer Debug::mvpBuffer;
ShaderBindingPoint Debug::mvpBindingPoint, Debug::colorBindingPoint;
ShaderPipeline Debug::pipeline;
RasterState Debug::fillRasterState, Debug::wireframeRasterState;
Debug::DebugDrawObject Debug::objCube, Debug::objSphere;
std::list<Debug::DebugDrawRequest> Debug::requests;
std::mutex Debug::debugDrawMutex;

void Debug::DebugDrawObject::clear()
{
    va = nullptr;
    ib = nullptr;
}

void Debug::initCube()
{
    float verts[] = {// front
                     -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
                     // back
                     -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};

    auto vb = renderDevice->createVertexBuffer(sizeof(verts), verts, gl::Usage::Static);

    unsigned int indices[] = {// front
                              0, 1, 2, 2, 3, 0,
                              // right
                              1, 5, 6, 6, 2, 1,
                              // back
                              7, 6, 5, 5, 4, 7,
                              // left
                              4, 0, 3, 3, 7, 4,
                              // bottom
                              4, 5, 1, 1, 0, 4,
                              // top
                              3, 2, 6, 6, 7, 3};

    objCube.numIndices = sizeof(indices) / sizeof(unsigned int);
    objCube.ib = renderDevice->createIndexBuffer(sizeof(indices), indices, gl::IndexFormat::UInt, gl::Usage::Static);

    gl::VertexArrayDesc vaDesc;
    vaDesc.elementCount = 1;
    vaDesc.elements[0].name = "position";
    vaDesc.elements[0].type = gl::Type::Float;
    vaDesc.elements[0].size = 3;
    vaDesc.elements[0].buffer.index = 0;
    vaDesc.elements[0].buffer.offset = 0;
    vaDesc.elements[0].buffer.stride = 3 * sizeof(float);
    vaDesc.buffers[0] = vb;
    vaDesc.shaderPipeline = pipeline;
    objCube.va = renderDevice->createVertexArray(vaDesc);
}

void Debug::initSphere()
{
    int sectorCount = 10;
    int stackCount = 10;

    std::vector<float> vertices;

    float x, y, z, xz;

    float sectorStep = (float)(2 * M_PI / sectorCount);
    float stackStep = (float)(M_PI / stackCount);
    float sectorAngle, stackAngle;

    for (int i = 0; i <= stackCount; ++i)
    {
        stackAngle = (float)(M_PI / 2 - i * stackStep);
        xz = cosf(stackAngle);
        y = sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j)
        {
            sectorAngle = j * sectorStep;

            x = xz * cosf(sectorAngle);
            z = xz * sinf(sectorAngle);
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }

    auto vb = renderDevice->createVertexBuffer(vertices.size() * sizeof(float), &vertices[0], gl::Usage::Static);

    std::vector<unsigned int> indices;
    int k1, k2;
    for (int i = 0; i < stackCount; ++i)
    {
        k1 = i * (sectorCount + 1);
        k2 = k1 + sectorCount + 1;

        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2)
        {
            if (i != 0)
            {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }

            if (i != (stackCount - 1))
            {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }

    objSphere.numIndices = (unsigned int)indices.size();
    objSphere.ib = renderDevice->createIndexBuffer(indices.size() * sizeof(unsigned int), &indices[0],
                                                   gl::IndexFormat::UInt, gl::Usage::Static);

    gl::VertexArrayDesc vaDesc;
    vaDesc.elementCount = 1;
    vaDesc.elements[0].name = "position";
    vaDesc.elements[0].type = gl::Type::Float;
    vaDesc.elements[0].size = 3;
    vaDesc.elements[0].buffer.index = 0;
    vaDesc.elements[0].buffer.offset = 0;
    vaDesc.elements[0].buffer.stride = 3 * sizeof(float);
    vaDesc.buffers[0] = vb;
    vaDesc.shaderPipeline = pipeline;
    objSphere.va = renderDevice->createVertexArray(vaDesc);
}

void Debug::init(gl::RenderDevice& targetRenderDevice)
{
    renderDevice = &targetRenderDevice;

    auto vs = renderDevice->createShaderStage(gl::Stage::Vertex, R"(
            #version 330 core

            in vec3 position;

            uniform MVP
            {
                mat4 mvp;
            };

            void main()
            {
                gl_Position = mvp * vec4(position, 1.0f);
            }
        )");

    auto ps = renderDevice->createShaderStage(gl::Stage::Pixel, R"(
            #version 330 core

            out vec4 color;

            uniform vec3 objColor;

            void main()
            {
                color = vec4(objColor, 1.0f);
            }
        )");

    pipeline = renderDevice->createShaderPipeline(vs, ps);

    initCube();
    initSphere();

    mvpBuffer = renderDevice->createConstantBuffer(sizeof(glm::mat4), nullptr, gl::Usage::Dynamic);
    mvpBindingPoint = pipeline->getBindingPoint("MVP");
    colorBindingPoint = pipeline->getBindingPoint("objColor");

    RasterStateDesc rsDesc;

    rsDesc.rasterMode = RasterMode::Fill;
    fillRasterState = renderDevice->createRasterState(rsDesc);

    rsDesc.rasterMode = RasterMode::Wireframe;
    wireframeRasterState = renderDevice->createRasterState(rsDesc);
}

void Debug::drawCube(glm::vec3 center, glm::vec3 size, float time, glm::quat rotation, glm::vec3 color)
{
    debugDrawMutex.lock();
    requests.push_back(DebugDrawRequest{
        objCube, fillRasterState, glm::translate(center) * glm::toMat4(rotation) * glm::scale(size), time, color});
    debugDrawMutex.unlock();
}

void Debug::drawWireCube(glm::vec3 center, glm::vec3 size, float time, glm::quat rotation, glm::vec3 color)
{
    debugDrawMutex.lock();
    requests.push_back(DebugDrawRequest{
        objCube, wireframeRasterState, glm::translate(center) * glm::toMat4(rotation) * glm::scale(size), time, color});
    debugDrawMutex.unlock();
}

void Debug::drawSphere(glm::vec3 center, float radius, float time, glm::vec3 color)
{
    debugDrawMutex.lock();
    requests.push_back(DebugDrawRequest{objSphere, fillRasterState,
                                        glm::translate(center) * glm::scale(glm::vec3(radius)), time, color});
    debugDrawMutex.unlock();
}

void Debug::drawWireSphere(glm::vec3 center, float radius, float time, glm::vec3 color)
{
    debugDrawMutex.lock();
    requests.push_back(DebugDrawRequest{objSphere, wireframeRasterState,
                                        glm::translate(center) * glm::scale(glm::vec3(radius)), time, color});
    debugDrawMutex.unlock();
}

void Debug::flush(glm::mat4 vp, double deltaT)
{
    renderDevice->setShaderPipeline(pipeline);

    debugDrawMutex.lock();
    for (auto it = requests.begin(); it != requests.end();)
    {
        renderDevice->setVertexArray(it->obj.va);
        renderDevice->setIndexBuffer(it->obj.ib);
        mvpBindingPoint->bind(mvpBuffer);

        auto& mvp = *(glm::mat4*)mvpBuffer->map();
        mvp = vp * it->modelMatrix;
        mvpBuffer->unmap();

        colorBindingPoint->setConstant(it->color);

        renderDevice->setRasterState(it->rasterState);
        renderDevice->drawTrianglesIndexed(0, it->obj.numIndices);

        it->timeLeft -= deltaT;

        auto current = it++;
        if (current->timeLeft <= 0)
            requests.erase(current);
    }
    debugDrawMutex.unlock();
}

void Debug::terminate()
{
    mvpBuffer = nullptr;
    mvpBindingPoint = colorBindingPoint = nullptr;
    pipeline = nullptr;
    fillRasterState = wireframeRasterState = nullptr;
    objCube.clear();
    objSphere.clear();
    requests.clear();
}
