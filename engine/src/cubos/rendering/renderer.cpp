#include <cubos/rendering/renderer.hpp>

using namespace cubos::gl;
using namespace cubos::rendering;

Renderer::Renderer(io::Window& window) : window(window), renderDevice(window.getRenderDevice())
{
}

Renderer::ModelID Renderer::registerModelInternal(const std::vector<cubos::gl::Vertex>& vertices,
                                                  std::vector<uint32_t>& indices, cubos::gl::ShaderPipeline pipeline)
{
    RendererModel model;

    VertexBuffer vb =
        renderDevice.createVertexBuffer(vertices.size() * sizeof(cubos::gl::Vertex), &vertices[0], gl::Usage::Static);

    VertexArrayDesc vaDesc;
    vaDesc.elementCount = 3;
    vaDesc.elements[0].name = "position";
    vaDesc.elements[0].type = gl::Type::UInt;
    vaDesc.elements[0].size = 3;
    vaDesc.elements[0].buffer.index = 0;
    vaDesc.elements[0].buffer.offset = offsetof(cubos::gl::Vertex, position);
    vaDesc.elements[0].buffer.stride = sizeof(cubos::gl::Vertex);
    vaDesc.elements[1].name = "normal";
    vaDesc.elements[1].type = gl::Type::Float;
    vaDesc.elements[1].size = 3;
    vaDesc.elements[1].buffer.index = 0;
    vaDesc.elements[1].buffer.offset = offsetof(cubos::gl::Vertex, normal);
    vaDesc.elements[1].buffer.stride = sizeof(cubos::gl::Vertex);
    vaDesc.elements[2].name = "material";
    vaDesc.elements[2].type = gl::Type::UShort;
    vaDesc.elements[2].size = 1;
    vaDesc.elements[2].buffer.index = 0;
    vaDesc.elements[2].buffer.offset = offsetof(cubos::gl::Vertex, material);
    vaDesc.elements[2].buffer.stride = sizeof(cubos::gl::Vertex);
    vaDesc.buffers[0] = vb;
    vaDesc.shaderPipeline = pipeline;

    model.va = renderDevice.createVertexArray(vaDesc);
    model.ib = renderDevice.createIndexBuffer(indices.size() * sizeof(size_t), &indices[0], gl::IndexFormat::UInt,
                                              gl::Usage::Static);
    model.numIndices = indices.size();

    models.push_back(model);
    return models.size() - 1;
}

Renderer::PaletteID Renderer::registerPalette(const Palette& palette)
{
    auto materials = palette.getData();
    size_t size = sizeof(Material) * palette.getSize();
    auto cb = renderDevice.createConstantBuffer(size, materials, gl::Usage::Static);
    palettes.push_back(cb);
    return palettes.size() - 1;
}

void Renderer::setPalette(PaletteID paletteID)
{
    if (paletteID > palettes.size() - 1)
    {
        logError("DeferredRenderer::setPalette() failed: no palette was registered with paletteID \"{}\"", paletteID);
        return;
    }
    currentPalette = palettes[paletteID];
}

void Renderer::addPostProcessingPass(const PostProcessingPass& pass)
{
    postProcessingPasses.emplace_back(pass);
}

void Renderer::drawModel(ModelID modelID, glm::mat4 modelMat)
{
    if (modelID > models.size() - 1)
    {
        logError("DeferredRenderer::drawModel() failed: no model was registered with modelID \"{}\"", modelID);
        return;
    }
    drawRequests.emplace_back(models[modelID], modelMat);
}

void Renderer::drawLight(const SpotLightData& light)
{
    spotLightRequests.push_back(light);
}

void Renderer::drawLight(const DirectionalLightData& light)
{
    directionalLightRequests.push_back(light);
}

void Renderer::drawLight(const PointLightData& light)
{
    pointLightRequests.push_back(light);
}

void Renderer::flush()
{
    drawRequests.clear();
    spotLightRequests.clear();
    directionalLightRequests.clear();
    pointLightRequests.clear();
}
