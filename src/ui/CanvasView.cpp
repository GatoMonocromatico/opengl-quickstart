#include "ui/CanvasView.h"

#include <algorithm>
#include <cmath>

namespace ui
{

glm::vec2 CanvasView::origin(const Rect& view) const
{
	return { std::round(view.x + offset.x), std::round(view.y + offset.y) };
}

void CanvasView::zoomAt(glm::vec2 screen, float factor, const Rect& view)
{
	const float z = std::clamp(zoom * factor, minZoom, maxZoom);
	// the world point drawn under the pointer stays under it: taken through the rounded
	// origin, the one on the screen, or the view creeps by its rounding every notch
	const glm::vec2 world = toWorld(screen, view);
	offset = screen - glm::vec2(view.x, view.y) - world * z;
	zoom = z;
}

void CanvasView::fit(const Rect& world, const Rect& view, float margin)
{
	const float zx = (view.w - 2.0f * margin) / std::max(world.w, 1e-3f);
	const float zy = (view.h - 2.0f * margin) / std::max(world.h, 1e-3f);
	zoom = std::clamp(std::min(zx, zy), minZoom, maxZoom);
	offset = centered({ world.x + world.w * 0.5f, world.y + world.h * 0.5f }, view);
}

glm::vec2 CanvasView::centered(glm::vec2 world, const Rect& view) const
{
	return glm::vec2(view.w, view.h) * 0.5f - world * zoom;
}

}
