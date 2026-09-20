#include "../PerfMode.h"

#include "../../../State.h"
#include "../../Upscaling.h"

bool PerfMode::ConfigureResolution()
{
	auto& upscaling = globals::features::upscaling;
	if (!upscaling.ShouldEngagePerfMode())
		return false;
	latchedUpscaleMethod = static_cast<uint32_t>(upscaling.GetUpscaleMethod());

	D3D11_TEXTURE2D_DESC desc{};
	globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(Util::AsW32(&desc));
	eyeCount = globals::game::isVR ? 2u : 1u;
	displayWidth = desc.Width;
	displayHeight = desc.Height;
	latchedQualityMode = std::clamp(upscaling.settings.qualityMode, 0u, 4u);
	float ratio = Upscaling::GetQualityModeRatio(latchedQualityMode);
	const float explicitScale = globals::game::isVR ? upscaling.settings.vrRenderScale : 0.0f;
	explicitScaleLatched = std::isfinite(explicitScale) && explicitScale > 0.0f;
	if (explicitScaleLatched) {
		ratio = 1.0f / std::clamp(explicitScale, Upscaling::kVRRenderScaleMin, Upscaling::kVRRenderScaleMax);
		float bestDelta = FLT_MAX;
		for (uint32_t mode = 1; mode <= 4; ++mode) {
			const float delta = std::abs(Upscaling::GetQualityModeRatio(mode) - ratio);
			if (delta < bestDelta) {
				bestDelta = delta;
				latchedQualityMode = mode;
			}
		}
	}
	uint32_t eyeWidth = std::max(2u, uint32_t(displayWidth / eyeCount / ratio)) & ~1u;
	renderHeight = std::max(2u, uint32_t(displayHeight / ratio)) & ~1u;
	if (explicitScaleLatched && upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS)
		upscaling.streamline.ClampToDLSSRenderRange(latchedQualityMode, displayWidth / eyeCount, displayHeight, eyeWidth, renderHeight);
	renderWidth = eyeWidth * eyeCount;
	return renderWidth < displayWidth && renderHeight < displayHeight;
}

void PerfMode::UpdateViewPortHook::thunk(RE::BSGraphics::Renderer* renderer, uint32_t width, uint32_t height, bool matchTarget)
{
	func(renderer, width, height, matchTarget);
	auto& self = globals::features::upscaling.perfMode;
	if (!self.sceneActive)
		return;
	auto apply = [&](auto& runtime) {
		const auto slot = runtime.renderTargets[0];
		const bool sceneTarget = std::ranges::any_of(self.colors, [&](const auto& target) { return target.slot == slot; });
		const bool sceneDepth = slot == static_cast<decltype(slot)>(RE::RENDER_TARGETS::kNONE) &&
		                        std::ranges::any_of(self.depths, [&](const auto& target) { return static_cast<uint32_t>(target.slot) == static_cast<uint32_t>(runtime.depthStencil); });
		if (!sceneTarget && !sceneDepth)
			return;
		auto& vp = runtime.viewPort;
		if (uint32_t(vp.width) == self.displayWidth && uint32_t(vp.height) == self.displayHeight) {
			vp.width = float(self.renderWidth);
			vp.height = float(self.renderHeight);
		}
	};
	if (globals::game::isVR)
		apply(globals::game::shadowState->GetVRRuntimeData());
	else
		apply(globals::game::shadowState->GetRuntimeData());
}

void PerfMode::ResolveMenuBackground()
{
	auto* state = globals::state;
	auto& upscaling = globals::features::upscaling;
	if (upscaling.perfMode.IsHookActive() && state->IsMainOrLoadingMenuOpen() && !state->worldRenderedThisFrame)
		upscaling.PostDisplay();
}

void PerfMode::MenuCopyHook::thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	ResolveMenuBackground();
	func(shader, shape, param);
}

void PerfMode::MenuTonemapHook::thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	ResolveMenuBackground();
	func(shader, shape, param);
}

void PerfMode::MenuTonemapFadeHook::thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	ResolveMenuBackground();
	func(shader, shape, param);
}
