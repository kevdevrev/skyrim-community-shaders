#pragma once

#include "Utils/D3D.h"

/** @brief Compact scene resources with a native-resolution post-processing boundary. */
struct PerfMode
{
	/** @brief Allocate the scene set transactionally after engine target creation. */
	void SetupResources();
	/** @brief Restore engine-owned targets before their recreation. */
	void ReleaseResources();
	/** @brief Select compact targets and their coordinate system for scene rendering. */
	void BeginScene();
	/** @brief Seed the reconstruction output with a spatial fallback. */
	void PrepareOutput();
	/** @brief Restore native targets and resolve scene attributes for post-processing. */
	void ResolveScene();
	/** @brief Switch resource bindings without transferring their contents. */
	void SetSceneTargets(bool scene);
	/** @brief Preserve native allocation dimensions while a display feature initializes. */
	struct DisplayScope
	{
		explicit DisplayScope(PerfMode& owner) : owner(owner), restore(owner.sceneActive) { owner.SetSceneTargets(false); }
		~DisplayScope()
		{
			if (restore)
				owner.SetSceneTargets(true);
		}
		PerfMode& owner;
		bool restore;
	};

	bool IsEnabled() const { return ready; }
	bool IsHookActive() const { return sceneActive; }
	bool IsExplicitScaleLatched() const { return explicitScaleLatched; }
	bool IsDisplaySizeChanged() const { return false; }
	uint32_t GetLatchedQualityMode() const { return latchedQualityMode; }
	uint32_t GetLatchedUpscaleMethod() const { return latchedUpscaleMethod; }
	uint32_t GetDisplayEyeWidth() const { return displayWidth / eyeCount; }
	uint32_t GetDisplayEyeHeight() const { return displayHeight; }
	uint32_t GetRenderEyeWidth() const { return renderWidth / eyeCount; }
	uint32_t GetRenderEyeHeight() const { return renderHeight; }
	float2 GetDisplayScreenSize() const { return { float(displayWidth), float(displayHeight) }; }
	ID3D11Texture2D* GetTestTexture() const { return Util::AsReal(output.texture); }
	ID3D11ShaderResourceView* GetTestTextureSRV() const { return Util::AsReal(output.SRV); }
	ID3D11UnorderedAccessView* GetTestTextureUAV() const { return Util::AsReal(output.UAV); }
	ID3D11Texture2D* GetRefraTempTex() const { return Util::AsReal(sharpen.texture); }
	ID3D11ShaderResourceView* GetRefraTempSRV() const { return Util::AsReal(sharpen.SRV); }
	ID3D11UnorderedAccessView* GetRefraTempUAV() const { return Util::AsReal(sharpen.UAV); }

private:
	struct ColorTarget
	{
		RE::RENDER_TARGET slot;
		RE::BSGraphics::RenderTargetData native{}, scene{};
	};
	struct DepthTarget
	{
		RE::RENDER_TARGET_DEPTHSTENCIL slot;
		RE::BSGraphics::DepthStencilData native{}, scene{};
	};
	std::vector<ColorTarget> colors;
	std::vector<DepthTarget> depths;
	std::vector<winrt::com_ptr<ID3D11DeviceChild>> resources;
	RE::BSGraphics::RenderTargetData output{}, sharpen{};
	winrt::com_ptr<ID3D11VertexShader> fullscreenVS;
	winrt::com_ptr<ID3D11PixelShader> colorPS, depthPS, stencilPS, depthStencilPS;
	winrt::com_ptr<ID3D11SamplerState> linearSampler;
	winrt::com_ptr<ID3D11DepthStencilState> depthState, depthStencilState;
	std::array<winrt::com_ptr<ID3D11DepthStencilState>, 8> stencilStates;
	winrt::com_ptr<ID3D11Buffer> resolveCB;
	bool ready = false, sceneActive = false, viewportHookInstalled = false;
	bool explicitScaleLatched = false;
	uint32_t displayWidth = 0, displayHeight = 0, renderWidth = 0, renderHeight = 0, eyeCount = 1;
	uint32_t latchedQualityMode = 0;
	uint32_t latchedUpscaleMethod = 0;
	int nativeViewWidth = 0, nativeViewHeight = 0;

	bool ConfigureResolution();
	void CreateColorTarget(const RE::BSGraphics::RenderTargetData& source, RE::BSGraphics::RenderTargetData& target, uint32_t width, uint32_t height);
	void CreateDepthTarget(const RE::BSGraphics::DepthStencilData& source, RE::BSGraphics::DepthStencilData& target, uint32_t width, uint32_t height);
	void CreateResolveResources();
	void DrawResolve(ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* stencil, ID3D11RenderTargetView* target, ID3D11DepthStencilView* depth, bool interpolate, bool dejitter);

	struct UpdateViewPortHook
	{
		static void thunk(RE::BSGraphics::Renderer* renderer, uint32_t width, uint32_t height, bool matchTarget);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct MenuCopyHook
	{
		static void thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct MenuTonemapHook
	{
		static void thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct MenuTonemapFadeHook
	{
		static void thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	static void ResolveMenuBackground();
};
