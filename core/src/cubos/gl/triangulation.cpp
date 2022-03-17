
#include <vector>
#include <tuple>
#include <cubos/gl/vertex.hpp>
#include <cubos/gl/grid.hpp>
#include <cubos/gl/triangulation.hpp>

using namespace cubos::gl;
using namespace glm;
using std::vector;

void add_triangle_from_quad(vector<Triangle>& triangles, glm::vec3 bottomLeft, glm::vec3 bottomRight, glm::vec3 topLeft,
                            glm::vec3 topRight, uint16_t material_id)
{
    Triangle triangle_bl, triangle_tr;

    triangle_bl.v0.material = triangle_bl.v1.material = triangle_bl.v2.material = triangle_tr.v0.material =
        triangle_tr.v1.material = triangle_tr.v2.material = material_id;

    auto normal = glm::cross(topLeft - bottomLeft, bottomRight - bottomLeft);

    triangle_bl.v0.normal = triangle_bl.v1.normal = triangle_bl.v2.normal = triangle_tr.v0.normal =
        triangle_tr.v1.normal = triangle_tr.v2.normal = normal;

    triangle_bl.v0.position = topLeft;
    triangle_bl.v1.position = bottomRight;
    triangle_bl.v2.position = bottomLeft;

    triangle_tr.v0.position = topRight;
    triangle_tr.v1.position = bottomRight;
    triangle_tr.v2.position = topLeft;

    triangles.push_back(triangle_bl);
    triangles.push_back(triangle_tr);
}

vector<Triangle> Triangulation::Triangulate(const Grid& grid)
{
    glm::uvec3 grid_size = grid.getSize();
    vector<Triangle> triangles = vector<Triangle>();

    for (uint x = 0; x < grid_size.x; x++)
    {
        for (uint y = 0; y < grid_size.y; y++)
        {
            for (uint z = 0; z < grid_size.z; z++)
            {
                glm::uvec3 position = {x, y, z};

                uint16_t material_id = grid.get(position);

                if (material_id != 0)
                {

                    // Front Face
                    uint front_index = z + 1;
                    if (front_index < 0 || front_index >= grid_size.z ||
                        (front_index >= 0 && front_index < grid_size.z && grid.get({x, y, front_index}) == 0))
                    {
                        add_triangle_from_quad(triangles, position, position + glm::uvec3(1, 0, 0),
                                               position + glm::uvec3(0, 1, 0), position + glm::uvec3(1, 1, 0),
                                               material_id);
                    }

                    // Back Face
                    uint back_index = z - 1;
                    if (back_index < 0 || back_index >= grid_size.z ||
                        (back_index >= 0 && back_index < grid_size.z && grid.get({x, y, back_index}) == 0))
                    {
                        add_triangle_from_quad(triangles, position + glm::uvec3(1, 0, 1),
                                               position + glm::uvec3(0, 0, 1), position + glm::uvec3(1, 1, 1),
                                               position + glm::uvec3(0, 1, 1), material_id);
                    }

                    // Top Face
                    uint top_index = y + 1;
                    if (top_index < 0 || top_index >= grid_size.y ||
                        (top_index >= 0 && top_index < grid_size.y && grid.get({x, top_index, z}) == 0))
                    {
                        add_triangle_from_quad(triangles, position + glm::uvec3(1, 1, 0),
                                               position + glm::uvec3(1, 1, 1), position + glm::uvec3(0, 1, 0),
                                               position + glm::uvec3(0, 1, 1), material_id);
                    }

                    // Bottom Face
                    uint bottom_index = y - 1;
                    if (bottom_index < 0 || bottom_index >= grid_size.y ||
                        (bottom_index >= 0 && bottom_index < grid_size.y && grid.get({x, bottom_index, z}) == 0))
                    {
                        add_triangle_from_quad(triangles, position, position + glm::uvec3(0, 0, 1),
                                               position + glm::uvec3(1, 0, 0), position + glm::uvec3(1, 0, 1),
                                               material_id);
                    }

                    // Right Face
                    uint right_face = x + 1;
                    if (right_face < 0 || right_face >= grid_size.x ||
                        (right_face >= 0 && right_face < grid_size.x && grid.get({right_face, y, z}) == 0))
                    {

                        add_triangle_from_quad(triangles, position + glm::uvec3(1, 0, 0),
                                               position + glm::uvec3(1, 0, 1), position + glm::uvec3(1, 1, 0),
                                               position + glm::uvec3(1, 1, 1), material_id);
                    }

                    // Left face
                    uint left_face = x - 1;
                    if (left_face < 0 || left_face >= grid_size.x ||
                        (left_face >= 0 && left_face < grid_size.x && grid.get({left_face, y, z}) == 0))

                    {

                        add_triangle_from_quad(triangles, position + glm::uvec3(0, 0, 1), position,
                                               position + glm::uvec3(0, 1, 1), position + glm::uvec3(0, 1, 0),
                                               material_id);
                    }
                }
            }
        }
    }

    return triangles;
}