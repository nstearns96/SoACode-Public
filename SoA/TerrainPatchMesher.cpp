#include "stdafx.h"
#include "TerrainPatchMesher.h"

#include "VoxelSpaceConversions.h"
#include "SphericalTerrainComponentUpdater.h"
#include "TerrainPatchMeshManager.h"
#include "PlanetData.h"

#include <Vorb/graphics/GpuMemory.h>
#include <Vorb/graphics/GraphicsDevice.h>
#include <Vorb/TextureRecycler.hpp>

/// Debug colors for rendering faces with unique color
const color3 DebugColors[6] {
      color3(255, 0, 0), //TOP
      color3(0, 255, 0), //LEFT
      color3(0, 0, 255), //RIGHT
      color3(255, 255, 0), //FRONT
      color3(0, 255, 255), //BACK
      color3(255, 0, 255) //BOTTOM
};

TerrainVertex TerrainPatchMesher::verts[TerrainPatchMesher::VERTS_SIZE];
WaterVertex TerrainPatchMesher::waterVerts[TerrainPatchMesher::VERTS_SIZE];

ui16 TerrainPatchMesher::waterIndexGrid[PATCH_WIDTH][PATCH_WIDTH];
ui16 TerrainPatchMesher::waterIndices[PATCH_INDICES];
bool TerrainPatchMesher::waterQuads[PATCH_WIDTH - 1][PATCH_WIDTH - 1];

VGIndexBuffer TerrainPatchMesher::m_sharedIbo = 0; ///< Reusable CCW IBO


TerrainPatchMesher::TerrainPatchMesher(TerrainPatchMeshManager* meshManager,
                                       PlanetGenData* planetGenData) :
    m_meshManager(meshManager),
    m_planetGenData(planetGenData) {
    // Construct reusable index buffer object
    if (m_sharedIbo == 0) {
        generateIndices(m_sharedIbo);
    }

    m_radius = m_planetGenData->radius;
}

TerrainPatchMesher::~TerrainPatchMesher() {
    vg::GpuMemory::freeBuffer(m_sharedIbo);
}

void TerrainPatchMesher::buildMesh(OUT TerrainPatchMesh* mesh, const f32v3& startPos, WorldCubeFace cubeFace, float width,
                                            float heightData[PATCH_HEIGHTMAP_WIDTH][PATCH_HEIGHTMAP_WIDTH][4],
                                            bool isSpherical) {

    m_isSpherical = isSpherical;
    m_cubeFace = cubeFace;
    // Grab mappings so we can rotate the 2D grid appropriately
    if (m_isSpherical) {
        m_coordMapping = VoxelSpaceConversions::VOXEL_TO_WORLD[(int)m_cubeFace];
        m_startPos = startPos;
        m_startPos.y *= (f32)VoxelSpaceConversions::FACE_Y_MULTS[(int)m_cubeFace];
        m_coordMults = f32v2(VoxelSpaceConversions::FACE_TO_WORLD_MULTS[(int)m_cubeFace]);
    } else {
        m_coordMapping = i32v3(0, 1, 2);
        m_startPos = f32v3(startPos.x, 0.0f, startPos.z);
        m_coordMults = f32v2(1.0f);
    }
    
    f32 h;
    f32 angle;
    f32v3 tmpPos;
    int xIndex;
    int zIndex;
    f32 minX = (f32)INT_MAX, maxX = (f32)INT_MIN;
    f32 minY = (f32)INT_MAX, maxY = (f32)INT_MIN;
    f32 minZ = (f32)INT_MAX, maxZ = (f32)INT_MIN;

    // Clear water index grid
    memset(waterIndexGrid, 0, sizeof(waterIndexGrid));
    memset(waterQuads, 0, sizeof(waterQuads));
    m_waterIndex = 0;
    m_waterIndexCount = 0;

    // Loop through and set all vertex attributes
    m_vertWidth = width / (PATCH_WIDTH - 1);
    m_index = 0;

    for (int z = 0; z < PATCH_WIDTH; z++) {
        for (int x = 0; x < PATCH_WIDTH; x++) {

            auto& v = verts[m_index];

            // Set the position based on which face we are on
            v.position[m_coordMapping.x] = (x * m_vertWidth + m_startPos.x) * m_coordMults.x;
            v.position[m_coordMapping.y] = m_startPos.y;
            v.position[m_coordMapping.z] = (z * m_vertWidth + m_startPos.z) * m_coordMults.y;

            // Set color
            v.color = m_planetGenData->terrainTint;
            // v.color = DebugColors[(int)mesh->m_cubeFace]; // Uncomment for unique face colors

            // TODO(Ben): This is temporary edge debugging stuff
            const float delta = 100.0f;
            if (abs(v.position[m_coordMapping.x]) >= m_radius - delta
                || abs(v.position[m_coordMapping.z]) >= m_radius - delta) {
                v.color.r = 255;
                v.color.g = 0;
                v.color.b = 0;
            }

            // Get data from heightmap 
            zIndex = z * PATCH_NORMALMAP_PIXELS_PER_QUAD + 1;
            xIndex = x * PATCH_NORMALMAP_PIXELS_PER_QUAD + 1;
            h = heightData[zIndex][xIndex][0] * KM_PER_M;

            // Water indexing
            if (h < 0) {
                addWater(z, x, heightData);
            }

            // Set texture coordinates using grid position
            v.texCoords.x = v.position[m_coordMapping.x];
            v.texCoords.y = v.position[m_coordMapping.z];

            // Set normal map texture coordinates
            v.normTexCoords.x = (ui8)(((float)x / (float)(PATCH_WIDTH - 1)) * 255.0f);
            v.normTexCoords.y = (ui8)(((float)z / (float)(PATCH_WIDTH - 1)) * 255.0f);

            // Spherify it!
            f32v3 normal;
            if (m_isSpherical) {
                normal = glm::normalize(v.position);
                v.position = normal * (m_radius + h);
            } else {
                const i32v3& trueMapping = VoxelSpaceConversions::VOXEL_TO_WORLD[(int)m_cubeFace];
                tmpPos[trueMapping.x] = v.position.x;
                tmpPos[trueMapping.y] = m_radius * (f32)VoxelSpaceConversions::FACE_Y_MULTS[(int)m_cubeFace];
                tmpPos[trueMapping.z] = v.position.z;
                normal = glm::normalize(tmpPos);
                v.position.y += h;
            }

            angle = computeAngleFromNormal(normal);
            // TODO(Ben): Only update when not in frustum. Use double frustum method to start loading at frustum 2 and force in frustum 1
            v.temperature = calculateTemperature(m_planetGenData->tempLatitudeFalloff, angle, heightData[zIndex][xIndex][1] - glm::max(0.0f, m_planetGenData->tempHeightFalloff * h));
            v.humidity = calculateHumidity(m_planetGenData->humLatitudeFalloff, angle, heightData[zIndex][xIndex][2] - glm::max(0.0f, m_planetGenData->humHeightFalloff * h));

            // Compute tangent
            tmpPos[m_coordMapping.x] = ((x + 1) * m_vertWidth + m_startPos.x) * m_coordMults.x;
            tmpPos[m_coordMapping.y] = m_startPos.y;
            tmpPos[m_coordMapping.z] = (z * m_vertWidth + m_startPos.z) * m_coordMults.y;
            tmpPos = glm::normalize(tmpPos) * (m_radius + h);
            v.tangent = glm::normalize(tmpPos - v.position);

            // Make sure tangent is orthogonal
            f32v3 binormal = glm::normalize(glm::cross(glm::normalize(v.position), v.tangent));
            v.tangent = glm::normalize(glm::cross(binormal, glm::normalize(v.position)));

            // Check bounding box
            // TODO(Ben): Worry about water too!
            if (v.position.x < minX) minX = v.position.x;
            if (v.position.x > maxX) maxX = v.position.x;
            if (v.position.y < minY) minY = v.position.y;
            if (v.position.y > maxY) maxY = v.position.y;
            if (v.position.z < minZ) minZ = v.position.z;
            if (v.position.z > maxZ) maxZ = v.position.z;

            m_index++;
        }
    }

    // Get AABB
    mesh->m_aabbPos = f32v3(minX, minY, minZ);
    mesh->m_aabbDims = f32v3(maxX - minX, maxY - minY, maxZ - minZ);
    mesh->m_aabbCenter = mesh->m_aabbPos + mesh->m_aabbDims * 0.5f;
    // Calculate bounding sphere for culling
    mesh->m_boundingSphereRadius = glm::length(mesh->m_aabbCenter - mesh->m_aabbPos);
    // Build the skirts for crack hiding
    buildSkirts();

    // Make all vertices relative to the aabb pos for far terrain
    if (!m_isSpherical) {
        for (int i = 0; i < m_index; i++) {
            verts[i].position = f32v3(f64v3(verts[i].position) - f64v3(mesh->m_aabbPos));
        }
    }

    // Generate the buffers and upload data
    vg::GpuMemory::createBuffer(mesh->m_vbo);
    vg::GpuMemory::bindBuffer(mesh->m_vbo, vg::BufferTarget::ARRAY_BUFFER);
    vg::GpuMemory::uploadBufferData(mesh->m_vbo, vg::BufferTarget::ARRAY_BUFFER,
                                    VERTS_SIZE * sizeof(TerrainVertex),
                                    verts);
    // Reusable IBO
    mesh->m_ibo = m_sharedIbo;

    // Add water mesh
    if (m_waterIndexCount) {
        // Make all vertices relative to the aabb pos for far terrain
        if (!m_isSpherical) {
            for (int i = 0; i < m_index; i++) {
                waterVerts[i].position -= mesh->m_aabbPos;
            }
        }

        mesh->m_waterIndexCount = m_waterIndexCount;
        vg::GpuMemory::createBuffer(mesh->m_wvbo);
        vg::GpuMemory::bindBuffer(mesh->m_wvbo, vg::BufferTarget::ARRAY_BUFFER);
        vg::GpuMemory::uploadBufferData(mesh->m_wvbo, vg::BufferTarget::ARRAY_BUFFER,
                                        m_waterIndex * sizeof(WaterVertex),
                                        waterVerts);
        vg::GpuMemory::createBuffer(mesh->m_wibo);
        vg::GpuMemory::bindBuffer(mesh->m_wibo, vg::BufferTarget::ELEMENT_ARRAY_BUFFER);
        vg::GpuMemory::uploadBufferData(mesh->m_wibo, vg::BufferTarget::ELEMENT_ARRAY_BUFFER,
                                        m_waterIndexCount * sizeof(ui16),
                                        waterIndices);
    }

    // Finally, add to the mesh manager
    m_meshManager->addMesh(mesh, isSpherical);

    // TODO: Using a VAO makes it not work??
    //    glBindVertexArray(0);
}

// Thanks to tetryds for these
ui8 TerrainPatchMesher::calculateTemperature(float range, float angle, float baseTemp) {
    float tempFalloff = 1.0f - pow(cos(angle), 2.0f * angle);
    float temp = baseTemp - tempFalloff * range;
    return (ui8)(glm::clamp(temp, 0.0f, 255.0f));
}

// Thanks to tetryds for these
ui8 TerrainPatchMesher::calculateHumidity(float range, float angle, float baseHum) {
    float cos3x = cos(3.0f * angle);
    float humFalloff = 1.0f - (-0.25f * angle + 1.0f) * (cos3x * cos3x);
    float hum = baseHum - humFalloff * range;
    return (ui8)(255.0f - glm::clamp(hum, 0.0f, 255.0f));
}

void TerrainPatchMesher::buildSkirts() {
    const float SKIRT_DEPTH = m_vertWidth * 3.0f;
    // Top Skirt
    for (int i = 0; i < PATCH_WIDTH; i++) {
        auto& v = verts[m_index];
        // Copy the vertices from the top edge
        v = verts[i];
        // Extrude downward
        if (m_isSpherical) {
            float len = glm::length(v.position) - SKIRT_DEPTH;
            v.position = glm::normalize(v.position) * len;
        } else {
            v.position.y -= SKIRT_DEPTH;
        }
        m_index++;
    }
    // Left Skirt
    for (int i = 0; i < PATCH_WIDTH; i++) {
        auto& v = verts[m_index];
        // Copy the vertices from the left edge
        v = verts[i * PATCH_WIDTH];
        // Extrude downward
        if (m_isSpherical) {
            float len = glm::length(v.position) - SKIRT_DEPTH;
            v.position = glm::normalize(v.position) * len;
        } else {
            v.position.y -= SKIRT_DEPTH;
        }
        m_index++;
    }
    // Right Skirt
    for (int i = 0; i < PATCH_WIDTH; i++) {
        auto& v = verts[m_index];
        // Copy the vertices from the right edge
        v = verts[i * PATCH_WIDTH + PATCH_WIDTH - 1];
        // Extrude downward
        if (m_isSpherical) {
            float len = glm::length(v.position) - SKIRT_DEPTH;
            v.position = glm::normalize(v.position) * len;
        } else {
            v.position.y -= SKIRT_DEPTH;
        }
        m_index++;
    }
    // Bottom Skirt
    for (int i = 0; i < PATCH_WIDTH; i++) {
        auto& v = verts[m_index];
        // Copy the vertices from the bottom edge
        v = verts[PATCH_SIZE - PATCH_WIDTH + i];
        // Extrude downward
        if (m_isSpherical) {
            float len = glm::length(v.position) - SKIRT_DEPTH;
            v.position = glm::normalize(v.position) * len;
        } else {
            v.position.y -= SKIRT_DEPTH;
        }
        m_index++;
    }
}

void TerrainPatchMesher::addWater(int z, int x, float heightData[PATCH_HEIGHTMAP_WIDTH][PATCH_HEIGHTMAP_WIDTH][4]) {
    // Try add all adjacent vertices if needed
    tryAddWaterVertex(z - 1, x - 1, heightData);
    tryAddWaterVertex(z - 1, x, heightData);
    tryAddWaterVertex(z - 1, x + 1, heightData);
    tryAddWaterVertex(z, x - 1, heightData);
    tryAddWaterVertex(z, x, heightData);
    tryAddWaterVertex(z, x + 1, heightData);
    tryAddWaterVertex(z + 1, x - 1, heightData);
    tryAddWaterVertex(z + 1, x, heightData);
    tryAddWaterVertex(z + 1, x + 1, heightData);

    // Try add quads
    tryAddWaterQuad(z - 1, x - 1);
    tryAddWaterQuad(z - 1, x);
    tryAddWaterQuad(z, x - 1);
    tryAddWaterQuad(z, x);
}

void TerrainPatchMesher::tryAddWaterVertex(int z, int x, float heightData[PATCH_HEIGHTMAP_WIDTH][PATCH_HEIGHTMAP_WIDTH][4]) {
    // TEMPORARY? Add slight offset so we don't need skirts
    float mvw = m_vertWidth * 1.005;
    const float UV_SCALE = 0.04;
    int xIndex;
    int zIndex;

    if (z < 0 || x < 0 || z >= PATCH_WIDTH || x >= PATCH_WIDTH) return;
    if (waterIndexGrid[z][x] == 0) {
        waterIndexGrid[z][x] = m_waterIndex + 1;
        auto& v = waterVerts[m_waterIndex];
        // Set the position based on which face we are on
        v.position[m_coordMapping.x] = (x * mvw + m_startPos.x) * m_coordMults.x;
        v.position[m_coordMapping.y] = m_startPos.y;
        v.position[m_coordMapping.z] = (z * mvw + m_startPos.z) * m_coordMults.y;

        // Set texture coordinates
        v.texCoords.x = v.position[m_coordMapping.x] * UV_SCALE;
        v.texCoords.y = v.position[m_coordMapping.z] * UV_SCALE;

        // Spherify it!
        f32v3 normal;
        if (m_isSpherical) {
            normal = glm::normalize(v.position);
            v.position = normal * m_radius;
        } else {
            const i32v3& trueMapping = VoxelSpaceConversions::VOXEL_TO_WORLD[(int)m_cubeFace];
            f32v3 tmpPos;
            tmpPos[trueMapping.x] = v.position.x;
            tmpPos[trueMapping.y] = m_radius * (f32)VoxelSpaceConversions::FACE_Y_MULTS[(int)m_cubeFace];
            tmpPos[trueMapping.z] = v.position.z;
            normal = glm::normalize(tmpPos);
        }

        zIndex = z * PATCH_NORMALMAP_PIXELS_PER_QUAD + 1;
        xIndex = x * PATCH_NORMALMAP_PIXELS_PER_QUAD + 1;
        float d = heightData[zIndex][xIndex][0];
        if (d < 0) {
            v.depth = -d;
        } else {
            v.depth = 0;
        }

        v.temperature = calculateTemperature(m_planetGenData->tempLatitudeFalloff, computeAngleFromNormal(normal), heightData[zIndex][xIndex][1]);

        // Compute tangent
        f32v3 tmpPos;
        tmpPos[m_coordMapping.x] = ((x + 1) * mvw + m_startPos.x) * m_coordMults.x;
        tmpPos[m_coordMapping.y] = m_startPos.y;
        tmpPos[m_coordMapping.z] = (z * mvw + m_startPos.z) * m_coordMults.y;
        tmpPos = glm::normalize(tmpPos) * m_radius;
        v.tangent = glm::normalize(tmpPos - v.position);

        // Make sure tangent is orthogonal
        f32v3 binormal = glm::normalize(glm::cross(glm::normalize(v.position), v.tangent));
        v.tangent = glm::normalize(glm::cross(binormal, glm::normalize(v.position)));

        v.color = m_planetGenData->liquidTint;
        m_waterIndex++;
    }
}

void TerrainPatchMesher::tryAddWaterQuad(int z, int x) {
    if (z < 0 || x < 0 || z >= PATCH_WIDTH - 1 || x >= PATCH_WIDTH - 1) return;
    if (!waterQuads[z][x]) {
        waterQuads[z][x] = true;
        waterIndices[m_waterIndexCount++] = waterIndexGrid[z][x] - 1;
        waterIndices[m_waterIndexCount++] = waterIndexGrid[z + 1][x] - 1;
        waterIndices[m_waterIndexCount++] = waterIndexGrid[z + 1][x + 1] - 1;
        waterIndices[m_waterIndexCount++] = waterIndexGrid[z + 1][x + 1] - 1;
        waterIndices[m_waterIndexCount++] = waterIndexGrid[z][x + 1] - 1;
        waterIndices[m_waterIndexCount++] = waterIndexGrid[z][x] - 1;
    }
}

void TerrainPatchMesher::generateIndices(OUT VGIndexBuffer& ibo) {
    // Loop through each quad and set indices
    int vertIndex;
    int index = 0;
    int skirtIndex = PATCH_SIZE;
    ui16 indices[PATCH_INDICES];
    
    // Main vertices
    for (int z = 0; z < PATCH_WIDTH - 1; z++) {
        for (int x = 0; x < PATCH_WIDTH - 1; x++) {
            // Compute index of back left vertex
            vertIndex = z * PATCH_WIDTH + x;
            // Change triangle orientation based on odd or even
            if ((x + z) % 2) {
                indices[index++] = vertIndex;
                indices[index++] = vertIndex + PATCH_WIDTH;
                indices[index++] = vertIndex + PATCH_WIDTH + 1;
                indices[index++] = vertIndex + PATCH_WIDTH + 1;
                indices[index++] = vertIndex + 1;
                indices[index++] = vertIndex;
            } else {
                indices[index++] = vertIndex + 1;
                indices[index++] = vertIndex;
                indices[index++] = vertIndex + PATCH_WIDTH;
                indices[index++] = vertIndex + PATCH_WIDTH;
                indices[index++] = vertIndex + PATCH_WIDTH + 1;
                indices[index++] = vertIndex + 1;
            }
        }
    }
    // Skirt vertices
    // Top Skirt
    for (int i = 0; i < PATCH_WIDTH - 1; i++) {
        vertIndex = i;
        indices[index++] = skirtIndex;
        indices[index++] = vertIndex;
        indices[index++] = vertIndex + 1;
        indices[index++] = vertIndex + 1;
        indices[index++] = skirtIndex + 1;
        indices[index++] = skirtIndex;
        skirtIndex++;
    }
    skirtIndex++; // Skip last vertex
    // Left Skirt
    for (int i = 0; i < PATCH_WIDTH - 1; i++) {
        vertIndex = i * PATCH_WIDTH;
        indices[index++] = skirtIndex;
        indices[index++] = skirtIndex + 1;
        indices[index++] = vertIndex + PATCH_WIDTH;
        indices[index++] = vertIndex + PATCH_WIDTH;
        indices[index++] = vertIndex;
        indices[index++] = skirtIndex;
        skirtIndex++;
    }
    skirtIndex++; // Skip last vertex
    // Right Skirt
    for (int i = 0; i < PATCH_WIDTH - 1; i++) {
        vertIndex = i * PATCH_WIDTH + PATCH_WIDTH - 1;
        indices[index++] = vertIndex;
        indices[index++] = vertIndex + PATCH_WIDTH;
        indices[index++] = skirtIndex + 1;
        indices[index++] = skirtIndex + 1;
        indices[index++] = skirtIndex;
        indices[index++] = vertIndex;
        skirtIndex++;
    }
    skirtIndex++;
    // Bottom Skirt
    for (int i = 0; i < PATCH_WIDTH - 1; i++) {
        vertIndex = PATCH_SIZE - PATCH_WIDTH + i;
        indices[index++] = vertIndex;
        indices[index++] = skirtIndex;
        indices[index++] = skirtIndex + 1;
        indices[index++] = skirtIndex + 1;
        indices[index++] = vertIndex + 1;
        indices[index++] = vertIndex;
        skirtIndex++;
    }

    vg::GpuMemory::createBuffer(ibo);
    vg::GpuMemory::bindBuffer(ibo, vg::BufferTarget::ELEMENT_ARRAY_BUFFER);
    vg::GpuMemory::uploadBufferData(ibo, vg::BufferTarget::ELEMENT_ARRAY_BUFFER,
                                    PATCH_INDICES * sizeof(ui16),
                                    indices);
}

float TerrainPatchMesher::computeAngleFromNormal(const f32v3& normal) {
    // Compute angle
    if (normal.y == 1.0f || normal.y == -1.0f) {
        return M_PI / 2.0;
    } else if (abs(normal.y) < 0.001) {
        // Need to do this to fix an equator bug
        return 0.0f;
    } else {
        f32v3 equator = glm::normalize(f32v3(normal.x, 0.0f, normal.z));
        return acos(glm::dot(equator, normal));
    }
}