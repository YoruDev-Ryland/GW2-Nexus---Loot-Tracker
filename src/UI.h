#pragma once

namespace UI
{
    // Registered with ERenderType RT_Render — draws the main loot window.
    void Render();

    // Registered with ERenderType RT_OptionsRender — draws settings in the
    // Nexus Options panel.
    void RenderOptions();
}
