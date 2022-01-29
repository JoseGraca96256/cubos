#ifndef CUBOS_GL_CAMERA_DATA_HPP
#define CUBOS_GL_CAMERA_DATA_HPP

#include <cubos/gl/render_device.hpp>
#include <glm/glm.hpp>

namespace cubos::gl
{
    struct CameraData
    {
        glm::mat4 viewMatrix; ///< The Camera's view matrix.

        glm::mat4 perspectiveMatrix; ///< The camera's perspective matrix.

        gl::Framebuffer target; ///< The target framebuffer which the camera should be used to draw into.
    };
} // namespace cubos::gl

#endif // CORE_INCLUDE_CUBOS_GL_CAMERA_DATA_HPP
