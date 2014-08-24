#include "stdafx.h"
#include "GraphicsDevice.h"

#include <SDL\SDL.h>

GraphicsDevice::GraphicsDevice() : _props({}) {
}

void GraphicsDevice::refreshInformation() {
    // Whenever Information Is Refreshed, The Current Device Is Refreshed
    _current = this;

    // Get Display Information
    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    _props.nativeScreenWidth = displayMode.w;
    _props.nativeScreenHeight = displayMode.h;
    _props.nativeRefreshRate = displayMode.refresh_rate;

    // Get The OpenGL Implementation Information
    _props.glVendor = (const cString)glGetString(GL_VENDOR);
    _props.glVersion = (const cString)glGetString(GL_VERSION);
    glGetIntegerv(GL_MAJOR_VERSION, &_props.glVersionMajor);
    glGetIntegerv(GL_MINOR_VERSION, &_props.glVersionMinor);
    _props.glslVersion = (const cString)glGetString(GL_SHADING_LANGUAGE_VERSION);

    // Get Vertex Information
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &_props.maxVertexAttributes);

    // Get MSAA Information
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &_props.maxColorSamples);
    glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &_props.maxDepthSamples);
    
    // Get Texture Unit Information
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &_props.maxTextureUnits);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &_props.maxTextureSize);
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &_props.max3DTextureSize);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &_props.maxArrayTextureLayers);

#ifdef DEBUG
    printf("Graphics Device Information Refreshed:\n");

    printf("\n=== OpenGL Implementation ===\n");
    printf("Vendor:                   %s\n", _props.glVendor);
    printf("GL Version:               %s\n", _props.glVersion);
    printf("GL Version (Strict):      %d.%d\n", _props.glVersionMajor, _props.glVersionMinor);
    printf("GLSL Version:             %s\n", _props.glslVersion);

    printf("\n=== Vertex Properties ===\n");
    printf("Max Vertex Attributes:    %d\n", _props.maxVertexAttributes);

    printf("\n=== Texture Properties ===\n");
    printf("Max Texture Units:        %d\n", _props.maxTextureUnits);
    printf("Max Texture Size:         %d\n", _props.maxTextureSize);
    printf("Max 3D Texture Size:      %d\n", _props.max3DTextureSize);
    printf("Max Array Texture Layers: %d\n", _props.maxArrayTextureLayers);
#endif // DEBUG
}

GraphicsDevice* GraphicsDevice::_current = nullptr;