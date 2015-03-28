///
/// SystemARRenderer.h
/// Seed of Andromeda
///
/// Created by Benjamin Arnold on 22 Feb 2015
/// Copyright 2014 Regrowth Studios
/// All Rights Reserved
///
/// Summary:
/// Augmented reality renderer for Space Systems
///

#pragma once

#ifndef SystemARRenderer_h__
#define SystemARRenderer_h__

#include <Vorb/graphics/gtypes.h>
#include <Vorb/VorbPreDecl.inl>
#include "OrbitComponentRenderer.h"

class Camera;
class MainMenuSystemViewer;
class SpaceSystem;

DECL_VG(class GLProgram;
        class SpriteBatch;
        class SpriteFont)

class SystemARRenderer {
public:
    ~SystemARRenderer();
    void draw(SpaceSystem* spaceSystem, const Camera* camera,
              OPT const MainMenuSystemViewer* systemViewer, VGTexture selectorTexture,
              const f32v2& viewport);

private:
    void buildShader();
    // Renders space paths
    void drawPaths();
    // Renders heads up display
    void drawHUD();

    vg::GLProgram* m_colorProgram = nullptr;
    vg::SpriteBatch* m_spriteBatch = nullptr;
    vg::SpriteFont* m_spriteFont = nullptr;

    // Helper variables to avoid passing
    SpaceSystem* m_spaceSystem = nullptr;
    const Camera* m_camera = nullptr;
    const MainMenuSystemViewer* m_systemViewer = nullptr;
    VGTexture m_selectorTexture = 0;
    f32v2 m_viewport;

    OrbitComponentRenderer m_orbitComponentRenderer;
};

#endif // SystemARRenderer_h__
