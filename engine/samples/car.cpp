#include <cubos/log.hpp>

#include <cubos/gl/render_device.hpp>
#include <cubos/gl/vertex.hpp>
#include <cubos/gl/palette.hpp>
#include <cubos/gl/grid.hpp>
#include <cubos/gl/debug.hpp>

#include <cubos/rendering/deferred/deferred_renderer.hpp>
#include <cubos/rendering/shadow_mapping/csm_shadow_mapper.hpp>
#include <cubos/rendering/post_processing/copy_pass.hpp>

#include <cubos/data/file_system.hpp>
#include <cubos/data/std_archive.hpp>
#include <cubos/data/qb_parser.hpp>

#include <cubos/io/window.hpp>
#include <cubos/io/input_manager.hpp>
#include <cubos/io/sources/double_axis.hpp>
#include <cubos/io/sources/single_axis.hpp>
#include <cubos/io/sources/button_press.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/ext.hpp>

#include <unordered_map>

using namespace cubos;

gl::Palette palette;

rendering::Renderer::ModelID registerModel(gl::Grid& grid, const gl::Palette& modelPalette,
                                           rendering::Renderer& renderer)
{
    for (int x = 0; x < grid.getSize().x; ++x)
        for (int y = 0; y < grid.getSize().y; ++y)
            for (int z = 0; z < grid.getSize().z; ++z)
            {
                auto index = grid.get(glm::vec3(x, y, z));
                if (index > 0)
                    grid.set(glm::vec3(x, y, z), index + palette.getSize());
            }

    auto origSize = palette.getSize();
    for (int i = 0; i < modelPalette.getSize(); ++i)
        palette.set(origSize + i + 1, modelPalette.get(i + 1));

    std::vector<gl::Vertex> vertices;
    std::vector<uint32_t> indices;
    gl::triangulate(grid, vertices, indices);

    return renderer.registerModel(vertices, indices);
}

class FreeCamera
{
private:
    bool enabled = true;
    glm::vec2 lastLook{-1};
    glm::vec3 pos{7, 7, -7};
    glm::vec2 orientation{45.0f * 3, 0.0f};
    glm::vec3 movement{0};

public:
    FreeCamera()
    {
        auto lookAction = io::InputManager::createAction("Look");
        lookAction->addBinding([&](io::Context ctx) {
            auto pos = ctx.getValue<glm::vec2>();
            pos.y = -pos.y;
            if (lastLook != glm::vec2(-1))
            {
                auto delta = pos - lastLook;
                orientation += delta * 0.1f;
                orientation.y = glm::clamp(orientation.y, -80.0f, 80.0f);
            }
            lastLook = pos;
        });
        lookAction->addSource(new io::DoubleAxis(cubos::io::MouseAxis::X, cubos::io::MouseAxis::Y));

        auto forwardAction = io::InputManager::createAction("Camera Forward");
        forwardAction->addBinding([&](io::Context ctx) {
            if (enabled)
                movement.z = ctx.getValue<float>();
        });
        forwardAction->addSource(new io::SingleAxis(io::Key::S, io::Key::W));

        auto strafeAction = io::InputManager::createAction("Camera Strafe");
        strafeAction->addBinding([&](io::Context ctx) {
            if (enabled)
                movement.x = ctx.getValue<float>();
        });
        strafeAction->addSource(new io::SingleAxis(io::Key::A, io::Key::D));

        auto verticalAction = io::InputManager::createAction("Camera Vertical");
        verticalAction->addBinding([&](io::Context ctx) {
            if (enabled)
                movement.y = ctx.getValue<float>();
        });
        verticalAction->addSource(new io::SingleAxis(io::Key::Q, io::Key::E));

        auto enableAction = io::InputManager::createAction("Enable Camera");
        enableAction->addBinding([&](io::Context ctx) { enabled = !enabled; });
        enableAction->addSource(new io::ButtonPress(io::Key::Space));
    }

    glm::vec3 getForward() const
    {
        auto o = glm::radians(orientation);
        return {cos(o.x) * cos(o.y), sin(o.y), sin(o.x) * cos(o.y)};
    }

    void update(float deltaT)
    {
        auto forward = getForward();
        auto right = glm::cross(forward, {0, 1, 0});
        auto up = glm::cross(right, forward);
        auto offset = (movement.z * forward + movement.x * right + movement.y * up) * deltaT * 2;
        pos += offset;
    }

    gl::CameraData getCameraData(const glm::vec2& windowSize)
    {
        return {glm::lookAt(pos, pos + getForward(), glm::vec3{0, 1, 0}),
                glm::radians(70.0f),
                windowSize.x / windowSize.y,
                0.1f,
                50.f,
                0};
    }
};

class Particle
{
private:
    rendering::Renderer* renderer;
    static rendering::Renderer::ModelID modelId;
    static glm::vec3 modelOffset;

public:
    glm::vec3 position{0};
    glm::quat rotation = glm::angleAxis(0.0f, glm::vec3(0, 1, 0));
    glm::vec3 scale{0.2f};
    glm::vec3 velocity{0};
    float age = 0;

    Particle(rendering::Renderer& renderer) : renderer(&renderer)
    {
        if (modelId == -1)
        {
            auto palette = gl::Palette({
                gl::Material{{1.0f, 0.5f, 0.5f, 1.0f}},
            });
            gl::Grid grid(glm::uvec3{1}, {1});
            modelOffset = glm::vec3{-0.5f};

            modelId = registerModel(grid, palette, renderer);
        }
    }

    void update(float deltaT)
    {
        position += velocity * deltaT;
        scale *= glm::max(0.0f, 1.0f - deltaT);
        age += deltaT;
    }

    void draw()
    {
        glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation) *
                             glm::scale(glm::mat4(1.0f), scale) * glm::translate(glm::mat4{1.0f}, modelOffset);
        renderer->drawModel(modelId, modelMat);
    }
};

rendering::Renderer::ModelID Particle::modelId = -1;
glm::vec3 Particle::modelOffset = {};

class Trail
{
private:
    rendering::Renderer& renderer;
    std::vector<Particle> particles;
    float t = 0.0f;
    float particlesMaxAge = 10.0f;
    size_t maxParticleCount = 100;

public:
    bool enabled = false;
    float timePerParticle = 0.02f;
    glm::vec3 position = {0.0f, 0.0f, 0.0f};

    Trail(rendering::Renderer& renderer) : renderer(renderer)
    {
    }

    void spawn()
    {
        Particle particle(renderer);
        particle.age = 0;
        particle.position = position;
        particle.velocity = glm::ballRand(0.5f) + glm::vec3{0.0f, 0.5f, 0.0f};

        size_t oldest = 0;
        for (size_t i = 0; i < particles.size(); ++i)
        {
            if (particles[i].age >= particlesMaxAge)
            {
                particles[i] = particle;
                return;
            }
            else if (particles[i].age >= particles[oldest].age)
                oldest = i;
        }

        if (particles.size() < maxParticleCount)
            particles.push_back(particle);
        else
            particles[oldest] = particle;
    }

    void update(float deltaT)
    {
        for (Particle& particle : particles)
            if (particle.age < particlesMaxAge)
                particle.update(deltaT);

        if (enabled)
            t += deltaT;

        while (t > timePerParticle)
        {
            t -= timePerParticle;
            spawn();
        }
    }

    void draw()
    {
        for (Particle& particle : particles)
            if (particle.age < particlesMaxAge)
                particle.draw();
    }
};

class Car
{
private:
    bool enabled = false;
    rendering::Renderer& renderer;
    rendering::Renderer::ModelID carId;
    float turnInput = 0;
    float accelerationInput = 0;
    float accelerationTime = 0.1f;
    float drag = 1.0f;
    float lateralDrag = 3.0f;
    float maxVelocity = 10;
    glm::vec3 modelOffset;
    glm::vec3 velocity{0.0f};
    float turnSpeed = 50.0f;
    float rotationVelocity = 0;
    float rotationMaxVelocity = 3.0f;
    float rotationDrag = 0.1f;

    glm::vec3 wheelOffsets[4];
    Trail trails[4];

public:
    glm::vec3 position{0};
    glm::quat rotation = glm::angleAxis(0.0f, glm::vec3(0, 1, 0));
    glm::vec3 scale{0.1f};

    explicit Car(rendering::Renderer& renderer)
        : renderer(renderer), trails{Trail(renderer), Trail(renderer), Trail(renderer), Trail(renderer)}
    {
        using namespace cubos::data;
        auto qb = FileSystem::find("/assets/car.qb");
        std::vector<QBMatrix> carModel;
        auto qbStream = qb->open(File::OpenMode::Read);
        parseQB(carModel, *qbStream);

        modelOffset = -glm::vec3(carModel[0].grid.getSize()) / 2.0f;
        modelOffset.y = 0;

        carId = registerModel(carModel[0].grid, carModel[0].palette, renderer);

        auto forwardAction = io::InputManager::createAction("Car Forward");
        forwardAction->addBinding([&](io::Context ctx) {
            if (enabled)
                accelerationInput = ctx.getValue<float>();
        });
        forwardAction->addSource(new io::SingleAxis(io::Key::S, io::Key::W));

        auto strafeAction = io::InputManager::createAction("Car Turn");
        strafeAction->addBinding([&](io::Context ctx) {
            if (enabled)
                turnInput = ctx.getValue<float>();
        });
        strafeAction->addSource(new io::SingleAxis(io::Key::A, io::Key::D));

        auto enableAction = io::InputManager::createAction("Enable Car");
        enableAction->addBinding([&](io::Context ctx) { enabled = !enabled; });
        enableAction->addSource(new io::ButtonPress(io::Key::Space));

        wheelOffsets[0] = {modelOffset.x, 0.0f, modelOffset.z};
        wheelOffsets[1] = {-modelOffset.x, 0.0f, modelOffset.z};
        wheelOffsets[2] = {-modelOffset.x, 0.0f, -modelOffset.z};
        wheelOffsets[3] = {modelOffset.x, 0.0f, -modelOffset.z};
        for (int i = 0; i < 4; ++i)
            wheelOffsets[i] *= 0.5f;
    }

    void draw()
    {
        glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation) *
                             glm::scale(glm::mat4(1.0f), scale) * glm::translate(glm::mat4{1.0f}, modelOffset);
        renderer.drawModel(carId, modelMat);
        for (int i = 0; i < 4; ++i)
            trails[i].draw();
    }

    void update(float deltaT)
    {
        glm::vec3 forward = rotation * glm::vec3(0, 0, 1.0f);
        glm::vec3 right = rotation * glm::vec3(1.0, 0, 0.0f);

        float absVelocity = glm::length(velocity);
        float forwardVelocity = glm::dot(forward, velocity);
        float drift = glm::dot(right, velocity);

        rotationVelocity *= glm::max(0.0f, 1.0f - rotationDrag * (1.0f + drift)) * deltaT;
        rotationVelocity += turnInput * turnSpeed * forwardVelocity * deltaT;
        rotationVelocity = glm::clamp(rotationVelocity, -rotationMaxVelocity, rotationMaxVelocity);
        rotation = glm::angleAxis(rotationVelocity * deltaT, glm::vec3(0, -1, 0)) * rotation;

        if (accelerationInput == 0.0f)
        {
            velocity *= glm::max(0.0f, 1.0f - drag * deltaT);
            if (absVelocity < 0.02f)
                velocity = glm::vec3(0.0f);
        }
        else
            velocity += forward * deltaT / accelerationTime * accelerationInput;

        velocity -= right * drift * lateralDrag * deltaT;

        position += velocity * deltaT;
        for (int i = 0; i < 4; ++i)
        {
            trails[i].position = position + rotation * wheelOffsets[i] * scale;
            trails[i].enabled = drift * drift > 1.0f;
            trails[i].update(deltaT);
        }
    }
};

class Floor
{
public:
    Floor(rendering::Renderer& renderer, Car& car) : renderer(renderer), car(car)
    {
        auto grid = gl::Grid({256, 1, 256});
        auto palette = gl::Palette({
            gl::Material{{0.5f, 0.5f, 0.5f, 1.0f}},
            gl::Material{{1.0f, 1.0f, 1.0f, 1.0f}},
        });

        for (int x = 0; x < 256; ++x)
            for (int z = 0; z < 256; ++z)
                grid.set({x, 0, z}, (x + z) % 2 + 1);

        floorId = registerModel(grid, palette, renderer);

        scale = {1.0f, 1.0f, 1.0f};
    }

    void draw()
    {
        position =
            glm::round(glm::vec3(car.position.x, 0, car.position.z) / 64.0f) * 64.0f - glm::vec3(128.0f, 1.0f, 128.0f);
        glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), scale);
        renderer.drawModel(floorId, modelMat);
    }

private:
    glm::vec3 position, scale;

    Car& car;
    rendering::Renderer& renderer;
    rendering::Renderer::ModelID floorId;
};

int main(void)
{
    initializeLogger();
    auto window = io::Window::create();
    window->setMouseLockState(io::MouseLockState::Locked);

    auto& renderDevice = window->getRenderDevice();

    gl::Debug::init(renderDevice);

    auto shadowMapper = rendering::CSMShadowMapper(renderDevice, 512, 2048, 256, 4);
    shadowMapper.setCascadeDistances({/**/ 3, 10, 24 /**/});

    auto renderer = rendering::DeferredRenderer(*window);

    renderer.setShadowMapper(&shadowMapper);

    data::FileSystem::mount("/assets", std::make_shared<data::STDArchive>(SAMPLE_ASSETS_FOLDER, true, false));
    io::InputManager::init(window);

    Car car(renderer);
    Floor floor(renderer, car);
    FreeCamera camera;

    auto paletteID = renderer.registerPalette(palette);
    renderer.setPalette(paletteID);

    rendering::CopyPass pass = rendering::CopyPass(*window);
    renderer.addPostProcessingPass(pass);
    float t = 0;
    float deltaT = 0;

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

        car.update(deltaT);
        car.draw();
        floor.draw();

        /*
        modelMat = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0)) *
                   glm::rotate(glm::mat4(1.0f), glm::radians(0.0f), axis) *
                   glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
        renderer.drawModel(floorId, modelMat);
         */

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

        camera.update(deltaT);

        renderer.render(camera.getCameraData(windowSize), false);
        renderer.flush();

        window->swapBuffers();
        window->pollEvents();
        io::InputManager::processActions();
    }

    delete window;
    return 0;
}
