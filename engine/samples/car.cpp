#include <cubos/log.hpp>
#include <cubos/io/window.hpp>
#include <cubos/gl/render_device.hpp>
#include <cubos/gl/vertex.hpp>
#include <cubos/gl/palette.hpp>
#include <cubos/gl/grid.hpp>
#include <cubos/gl/triangulation.hpp>
#include <cubos/rendering/deferred/deferred_renderer.hpp>
#include <cubos/rendering/shadow_mapping/csm_shadow_mapper.hpp>
#include <cubos/rendering/post_processing/copy_pass.hpp>
#include <cubos/data/file_system.hpp>
#include <cubos/data/std_archive.hpp>
#include <cubos/data/qb_parser.hpp>
#include <cubos/io/input_manager.hpp>
#include <cubos/io/sources/double_axis.hpp>
#include <cubos/io/sources/single_axis.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>
#include <unordered_map>

using namespace cubos;

int main(void)
{
    initializeLogger();
    auto window = io::Window::create();
    window->setMouseLockState(io::MouseLockState::Locked);

    auto& renderDevice = window->getRenderDevice();

    auto shadowMapper = rendering::CSMShadowMapper(renderDevice, 512, 2048, 256, 4);
    shadowMapper.setCascadeDistances({/**/ 3, 10, 24 /**/});

    auto renderer = rendering::DeferredRenderer(*window);

    renderer.setShadowMapper(&shadowMapper);

    using namespace cubos::data;

    FileSystem::mount("/assets", std::make_shared<STDArchive>(std::filesystem::current_path(), true, false));
    auto qb = FileSystem::find("/assets/tank.qb");
    std::vector<QBMatrix> model;
    auto qbStream = qb->open(File::OpenMode::Read);
    parseQB(model, *qbStream);

    auto paletteID = renderer.registerPalette(model[0].palette);
    renderer.setPalette(paletteID);

    std::vector<cubos::gl::Vertex> vertices;

    std::vector<uint32_t> indices;

    std::vector<cubos::gl::Triangle> triangles = cubos::gl::Triangulation::Triangulate(model[0].grid);

    std::unordered_map<cubos::gl::Vertex, int, cubos::gl::Vertex::hash> vertex_to_index;

    for (auto it = triangles.begin(); it != triangles.end(); it++)
    {

        int v0_index = -1;
        if (!vertex_to_index.contains(it->v0))
        {
            v0_index = vertex_to_index[it->v0] = vertices.size();
            vertices.push_back(it->v0);
        }
        else
        {
            v0_index = vertex_to_index[it->v0];
        }

        indices.push_back(v0_index);

        int v1_index = -1;
        if (!vertex_to_index.contains(it->v1))
        {
            v1_index = vertex_to_index[it->v1] = vertices.size();
            vertices.push_back(it->v1);
        }
        else
        {
            v1_index = vertex_to_index[it->v1];
        }

        indices.push_back(v1_index);

        int v2_index = -1;
        if (!vertex_to_index.contains(it->v2))
        {
            v2_index = vertex_to_index[it->v2] = vertices.size();
            vertices.push_back(it->v2);
        }
        else
        {
            v2_index = vertex_to_index[it->v2];
        }

        indices.push_back(v2_index);
    }

    rendering::Renderer::ModelID id = renderer.registerModel(vertices, indices);

    rendering::CopyPass pass = rendering::CopyPass(*window);
    renderer.addPostProcessingPass(pass);

    struct
    {
        glm::vec3 pos{7, 0, 7};
        glm::vec2 orientation{-45.0f * 3, 0.0f};

        glm::vec3 getForward() const
        {
            auto o = glm::radians(orientation);
            return {cos(o.x) * cos(o.y), sin(o.y), sin(o.x) * cos(o.y)};
        }
    } camera;

    glm::vec2 lastLook(-1);
    float t = 0;
    float deltaT = 0;

    io::InputManager::init(window);

    auto lookAction = io::InputManager::createAction("Look");
    lookAction->addBinding([&](io::Context ctx) {
        auto pos = ctx.getValue<glm::vec2>();
        pos.y = -pos.y;
        if (lastLook != glm::vec2(-1))
        {
            auto delta = pos - lastLook;
            camera.orientation += delta * 0.1f;
            camera.orientation.y = glm::clamp(camera.orientation.y, -80.0f, 80.0f);
        }
        lastLook = pos;
    });
    lookAction->addSource(new io::DoubleAxis(cubos::io::MouseAxis::X, cubos::io::MouseAxis::Y));

    glm::vec3 movement(0);

    auto forwardAction = io::InputManager::createAction("Forward");
    forwardAction->addBinding([&](io::Context ctx) { movement.z = ctx.getValue<float>(); });
    forwardAction->addSource(new io::SingleAxis(io::Key::S, io::Key::W));

    auto strafeAction = io::InputManager::createAction("Strafe");
    strafeAction->addBinding([&](io::Context ctx) { movement.x = ctx.getValue<float>(); });
    strafeAction->addSource(new io::SingleAxis(io::Key::A, io::Key::D));

    auto verticalAction = io::InputManager::createAction("Vertical");
    verticalAction->addBinding([&](io::Context ctx) { movement.y = ctx.getValue<float>(); });
    verticalAction->addSource(new io::SingleAxis(io::Key::Q, io::Key::E));

    glm::vec2 windowSize = window->getFramebufferSize();

    int s = 0;

    while (!window->shouldClose())
    {
        float currentT = window->getTime();
        if (t != 0)
        {
            deltaT = currentT - t;
            int newS = std::round(t);
            if (newS != s)
                logDebug("FPS: {}", std::round(1 / deltaT));
            s = newS;
        }
        t = currentT;
        renderDevice.setFramebuffer(0);
        renderDevice.clearColor(0.0, 0.0, 0.0, 0.0f);

        auto axis = glm::vec3(1, 0, 0);

        auto modelMat = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0)) *
                        glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.5f)) *
                        glm::rotate(glm::mat4(1.0f), glm::radians(t) * 10, axis) *
                        glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, -0.5f, -0.5f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.025f));
        renderer.drawModel(id, modelMat);

        glm::quat directionalLightRotation =
            glm::quat(glm::vec3(0, 0, 0)) * glm::quat(glm::vec3(glm::radians(45.0f), 0, 0));
        glm::quat pointLightRotation =
            glm::quat(glm::vec3(0, -t * 0.1f, 0)) * glm::quat(glm::vec3(glm::radians(30.0f), 0, 0));

        /*/
        glm::quat spotLightRotation =
            glm::quat(glm::vec3(0, -t * 0.5f, 0)) * glm::quat(glm::vec3(glm::radians(45.0f), 0, 0));
        renderer.drawLight(gl::SpotLightData(spotLightRotation * glm::vec3(0, 0, -5), spotLightRotation, glm::vec3(1),
                                             1, 20, glm::radians(10.0f), glm::radians(9.0f), false));
        spotLightRotation = glm::quat(glm::vec3(0, t * 0.5f, 0)) * glm::quat(glm::vec3(glm::radians(45.0f), 0, 0));
        renderer.drawLight(gl::SpotLightData(spotLightRotation * glm::vec3(0, 0, -5), spotLightRotation, glm::vec3(1),
                                             1, 20, glm::radians(10.0f), glm::radians(9.0f), false));
        /**/

        /**/
        renderer.drawLight(gl::DirectionalLightData(directionalLightRotation, glm::vec3(1), 1.0f, true));
        /**/

        /*/
        renderer.drawLight(gl::PointLightData(pointLightRotation * glm::vec3(0, 0, -2), glm::vec3(1), 1, 10, false));
        /**/

        auto forward = camera.getForward();
        auto right = glm::cross(forward, {0, 1, 0});
        auto up = glm::cross(right, forward);
        auto offset = (movement.z * forward + movement.x * right + movement.y * up) * deltaT * 2;
        camera.pos += offset;

        gl::CameraData mainCamera = {glm::lookAt(camera.pos, camera.pos + camera.getForward(), glm::vec3{0, 1, 0}),
                                     glm::radians(70.0f),
                                     windowSize.x / windowSize.y,
                                     0.1f,
                                     50.f,
                                     0};

        renderer.render(mainCamera, false);
        renderer.flush();

        window->swapBuffers();
        window->pollEvents();
        io::InputManager::processActions();
    }

    delete window;
    return 0;
}
