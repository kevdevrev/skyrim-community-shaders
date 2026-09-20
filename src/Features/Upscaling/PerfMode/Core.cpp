#include "../PerfMode.h"

#include <d3d11_3.h>

#include "../../../GpuPass.h"
#include "../../../State.h"
#include "../../Upscaling.h"

namespace
{
	template <class T, class Create>
	T* CreateOwned(std::vector<winrt::com_ptr<ID3D11DeviceChild>>& resources, const char* name, Create create)
	{
		winrt::com_ptr<T> value;
		winrt::check_hresult(create(value.put()));
		Util::SetResourceName(value.get(), "%s", name);
		auto* result = value.get();
		resources.push_back(value.template as<ID3D11DeviceChild>());
		return result;
	}

	struct ResolveConstants
	{
		float2 sourceSize;
		float2 destinationSize;
		float2 jitter;
		uint32_t eyes;
		uint32_t stencilBit;
		uint32_t interpolate;
		uint32_t padding[3]{};
	};
}

void PerfMode::CreateColorTarget(const RE::BSGraphics::RenderTargetData& source, RE::BSGraphics::RenderTargetData& target, uint32_t width, uint32_t height)
{
	auto* device = globals::d3d::device;
	D3D11_TEXTURE2D_DESC desc{};
	Util::AsReal(source.texture)->GetDesc(&desc);
	if (desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
		winrt::throw_hresult(E_INVALIDARG);
	desc.Width = width;
	desc.Height = height;
	auto texture = [&](auto* original, auto*& destination) {
		if (!original)
			return;
		destination = Util::AsW32(CreateOwned<ID3D11Texture2D>(resources, "SceneResolution::Color",
			[&](auto** value) { return device->CreateTexture2D(&desc, nullptr, value); }));
	};
	texture(source.texture, target.texture);
	texture(source.textureCopy, target.textureCopy);
	if (source.RTV) {
		D3D11_RENDER_TARGET_VIEW_DESC view{};
		Util::AsReal(source.RTV)->GetDesc(&view);
		target.RTV = Util::AsW32(CreateOwned<ID3D11RenderTargetView>(resources, "SceneResolution::Color RTV",
			[&](auto** value) { return device->CreateRenderTargetView(Util::AsReal(target.texture), &view, value); }));
	}
	auto srv = [&](auto* original, auto* resource, auto*& destination) {
		if (!original)
			return;
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};
		Util::AsReal(original)->GetDesc(&view);
		destination = Util::AsW32(CreateOwned<ID3D11ShaderResourceView>(resources, "SceneResolution::Color SRV",
			[&](auto** value) { return device->CreateShaderResourceView(Util::AsReal(resource), &view, value); }));
	};
	srv(source.SRV, target.texture, target.SRV);
	srv(source.SRVCopy, target.textureCopy, target.SRVCopy);
	if (source.UAV) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
		Util::AsReal(source.UAV)->GetDesc(&view);
		target.UAV = Util::AsW32(CreateOwned<ID3D11UnorderedAccessView>(resources, "SceneResolution::Color UAV",
			[&](auto** value) { return device->CreateUnorderedAccessView(Util::AsReal(target.texture), &view, value); }));
	}
}

void PerfMode::CreateDepthTarget(const RE::BSGraphics::DepthStencilData& source, RE::BSGraphics::DepthStencilData& target, uint32_t width, uint32_t height)
{
	auto* device = globals::d3d::device;
	D3D11_TEXTURE2D_DESC desc{};
	Util::AsReal(source.texture)->GetDesc(&desc);
	if (desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
		winrt::throw_hresult(E_INVALIDARG);
	desc.Width = width;
	desc.Height = height;
	target.texture = Util::AsW32(CreateOwned<ID3D11Texture2D>(resources, "SceneResolution::Depth",
		[&](auto** value) { return device->CreateTexture2D(&desc, nullptr, value); }));
	auto dsv = [&](auto* original, auto*& destination) {
		if (!original)
			return;
		D3D11_DEPTH_STENCIL_VIEW_DESC view{};
		Util::AsReal(original)->GetDesc(&view);
		destination = Util::AsW32(CreateOwned<ID3D11DepthStencilView>(resources, "SceneResolution::Depth DSV",
			[&](auto** value) { return device->CreateDepthStencilView(Util::AsReal(target.texture), &view, value); }));
	};
	for (size_t i = 0; i < 8; ++i) {
		dsv(source.views[i], target.views[i]);
		dsv(source.readOnlyViews[i], target.readOnlyViews[i]);
	}
	auto srv = [&](auto* original, auto*& destination) {
		if (!original)
			return;
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};
		Util::AsReal(original)->GetDesc(&view);
		destination = Util::AsW32(CreateOwned<ID3D11ShaderResourceView>(resources, "SceneResolution::Depth SRV",
			[&](auto** value) { return device->CreateShaderResourceView(Util::AsReal(target.texture), &view, value); }));
	};
	srv(source.depthSRV, target.depthSRV);
	srv(source.stencilSRV, target.stencilSRV);
}

void PerfMode::CreateResolveResources()
{
	fullscreenVS.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(
		L"Data/Shaders/Upscaling/UpscaleVS.hlsl", { { "VSHADER", "" } }, "vs_5_0")));
	colorPS.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(
		L"Data/Shaders/Upscaling/SceneResolvePS.hlsl", {}, "ps_5_0")));
	depthPS.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(
		L"Data/Shaders/Upscaling/SceneResolvePS.hlsl", { { "RESOLVE_DEPTH", "" } }, "ps_5_0")));
	stencilPS.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(
		L"Data/Shaders/Upscaling/SceneResolvePS.hlsl", { { "RESOLVE_STENCIL", "" } }, "ps_5_0")));
	if (!fullscreenVS || !colorPS || !depthPS || !stencilPS)
		winrt::throw_hresult(E_FAIL);

	auto* device = globals::d3d::device;
	D3D11_SAMPLER_DESC sampler{};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	winrt::check_hresult(device->CreateSamplerState(&sampler, linearSampler.put()));
	Util::SetResourceName(linearSampler.get(), "SceneResolution::LinearSampler");

	D3D11_BUFFER_DESC buffer{};
	buffer.ByteWidth = sizeof(ResolveConstants);
	buffer.Usage = D3D11_USAGE_DEFAULT;
	buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	winrt::check_hresult(device->CreateBuffer(&buffer, nullptr, resolveCB.put()));
	Util::SetResourceName(resolveCB.get(), "SceneResolution::ResolveConstants");

	D3D11_DEPTH_STENCIL_DESC ds{};
	ds.DepthEnable = true;
	ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
	winrt::check_hresult(device->CreateDepthStencilState(&ds, depthState.put()));
	Util::SetResourceName(depthState.get(), "SceneResolution::DepthWrite");
	ds.DepthEnable = false;
	ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	ds.StencilEnable = true;
	ds.StencilReadMask = 0xff;
	ds.FrontFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS };
	ds.BackFace = ds.FrontFace;
	for (uint32_t bit = 0; bit < 8; ++bit) {
		ds.StencilWriteMask = uint8_t(1u << bit);
		winrt::check_hresult(device->CreateDepthStencilState(&ds, stencilStates[bit].put()));
		Util::SetResourceName(stencilStates[bit].get(), "SceneResolution::StencilBit%u", bit);
	}
	D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
	if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options, sizeof(options))) && options.PSSpecifiedStencilRefSupported) {
		depthStencilPS.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(
			L"Data/Shaders/Upscaling/SceneResolvePS.hlsl", { { "RESOLVE_DEPTH_STENCIL", "" } }, "ps_5_0")));
		if (depthStencilPS) {
			ds.DepthEnable = true;
			ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			ds.StencilWriteMask = 0xff;
			winrt::check_hresult(device->CreateDepthStencilState(&ds, depthStencilState.put()));
			Util::SetResourceName(depthStencilState.get(), "SceneResolution::DepthStencilWrite");
		}
	}
}

void PerfMode::SetupResources()
{
	ReleaseResources();
	if (!ConfigureResolution())
		return;
	try {
		auto* renderer = globals::game::renderer;
		using RT = RE::RENDER_TARGETS;
		constexpr RE::RENDER_TARGET sceneColors[] = {
			RT::kMAIN, RT::kMAIN_COPY, RT::kMAIN_ONLY_ALPHA,
			RT::kNORMAL_TAAMASK_SSRMASK, RT::kNORMAL_TAAMASK_SSRMASK_SWAP,
			RT::kNORMAL_TAAMASK_SSRMASK_DOWNSAMPLED, RT::kMOTION_VECTOR,
			RT::kUNDERWATER_MASK, RT::kREFRACTION_NORMALS, RT::kSHADOW_MASK,
			RT::kTEMPORAL_AA_MASK, RT::kTEMPORAL_AA_ACCUMULATION_1, RT::kTEMPORAL_AA_ACCUMULATION_2,
			RT::kTEMPORAL_AA_WATER_1, RT::kTEMPORAL_AA_WATER_2, RT::kRAW_WATER,
			RT::kSNOW_SPECALPHA, RT::kSNOW_SWAP,
			RT::kIMAGESPACE_VOLUMETRIC_LIGHTING, RT::kIMAGESPACE_VOLUMETRIC_LIGHTING_PREVIOUS,
			RT::kIMAGESPACE_VOLUMETRIC_LIGHTING_COPY,
			RT::kVOLUMETRIC_LIGHTING_HALF_RES, RT::kVOLUMETRIC_LIGHTING_BLUR_HALF_RES,
			RT::kVOLUMETRIC_LIGHTING_QUARTER_RES, RT::kVOLUMETRIC_LIGHTING_BLUR_QUARTER_RES
		};
		for (auto slot : sceneColors) {
			auto& native = renderer->GetRuntimeData().renderTargets[slot];
			if (!native.texture)
				continue;
			D3D11_TEXTURE2D_DESC desc{};
			Util::AsReal(native.texture)->GetDesc(&desc);
			ColorTarget target{};
			target.slot = slot;
			target.native = native;
			const auto width = std::max(eyeCount, uint32_t(uint64_t(desc.Width) * renderWidth / displayWidth) / eyeCount * eyeCount);
			const auto height = std::max(1u, uint32_t(uint64_t(desc.Height) * renderHeight / displayHeight));
			CreateColorTarget(native, target.scene, width, height);
			colors.push_back(target);
		}
		using DS = RE::RENDER_TARGETS_DEPTHSTENCIL;
		constexpr RE::RENDER_TARGET_DEPTHSTENCIL sceneDepths[] = { DS::kMAIN, DS::kMAIN_COPY, DS::kPOST_ZPREPASS_COPY, DS::kPOST_WATER_COPY };
		for (auto slot : sceneDepths) {
			auto& native = renderer->GetDepthStencilData().depthStencils[slot];
			if (!native.texture)
				continue;
			DepthTarget target{};
			target.slot = slot;
			target.native = native;
			CreateDepthTarget(native, target.scene, renderWidth, renderHeight);
			depths.push_back(target);
		}
		auto main = renderer->GetRuntimeData().renderTargets[RT::kMAIN];
		main.textureCopy = nullptr;
		main.SRVCopy = nullptr;
		CreateColorTarget(main, output, displayWidth, displayHeight);
		CreateColorTarget(main, sharpen, displayWidth, displayHeight);
		CreateResolveResources();
		const float clear[4]{};
		for (const auto& target : colors) {
			if (target.scene.RTV)
				globals::d3d::context->ClearRenderTargetView(Util::AsReal(target.scene.RTV), clear);
		}
		if (!viewportHookInstalled) {
			stl::detour_thunk<UpdateViewPortHook>(REL::RelocationID(75455, 77240));
			stl::write_vfunc<0x1, MenuCopyHook>(RE::VTABLE_BSImagespaceShaderCopy[3]);
			stl::write_vfunc<0x1, MenuTonemapHook>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[3]);
			stl::write_vfunc<0x1, MenuTonemapFadeHook>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[3]);
			viewportHookInstalled = true;
		}
		ready = true;
		logger::info("[SceneResolution] {}x{} scene -> {}x{} output ({} eyes)", renderWidth, renderHeight, displayWidth, displayHeight, eyeCount);
	} catch (...) {
		ReleaseResources();
		logger::error("[SceneResolution] Resource setup failed; retaining native targets and standard upscaling");
	}
}

void PerfMode::SetSceneTargets(bool scene)
{
	if (!ready || scene == sceneActive)
		return;
	if (scene) {
		nativeViewWidth = *globals::game::viewWidth;
		nativeViewHeight = *globals::game::viewHeight;
		*globals::game::viewWidth = int(uint64_t(nativeViewWidth) * renderWidth / displayWidth);
		*globals::game::viewHeight = int(uint64_t(nativeViewHeight) * renderHeight / displayHeight);
	} else {
		*globals::game::viewWidth = nativeViewWidth;
		*globals::game::viewHeight = nativeViewHeight;
	}
	auto* context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);
	auto* renderer = globals::game::renderer;
	for (auto& target : colors) {
		auto& bound = renderer->GetRuntimeData().renderTargets[target.slot];
		(sceneActive ? target.scene : target.native) = bound;
		bound = scene ? target.scene : target.native;
	}
	for (auto& target : depths) {
		auto& bound = renderer->GetDepthStencilData().depthStencils[target.slot];
		(sceneActive ? target.scene : target.native) = bound;
		bound = scene ? target.scene : target.native;
	}
	sceneActive = scene;
	globals::state->screenSize = scene ? float2{ float(renderWidth), float(renderHeight) } : GetDisplayScreenSize();
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET, RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
}

void PerfMode::BeginScene()
{
	if (!ready)
		return;
	SetSceneTargets(true);
	auto& runtime = globals::game::graphicsState->GetRuntimeData();
	runtime.dynamicResolutionWidthRatio = runtime.dynamicResolutionHeightRatio = 1.0f;
	runtime.dynamicResolutionPreviousWidthRatio = runtime.dynamicResolutionPreviousHeightRatio = 1.0f;
	runtime.dynamicResolutionLock = 1;
	globals::features::upscaling.resolutionScale = { 1, 1 };
}

void PerfMode::ReleaseResources()
{
	SetSceneTargets(false);
	ready = false;
	colors.clear();
	depths.clear();
	output = {};
	sharpen = {};
	resources.clear();
	fullscreenVS = nullptr;
	colorPS = nullptr;
	depthPS = nullptr;
	stencilPS = nullptr;
	depthStencilPS = nullptr;
	linearSampler = nullptr;
	depthState = nullptr;
	depthStencilState = nullptr;
	for (auto& state : stencilStates)
		state = nullptr;
	resolveCB = nullptr;
}

void PerfMode::DrawResolve(ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* stencil, ID3D11RenderTargetView* target, ID3D11DepthStencilView* depth, bool interpolate, bool dejitter)
{
	if (!source || (!target && !depth))
		return;
	auto* context = globals::d3d::context;
	D3D11_TEXTURE2D_DESC sourceDesc{}, destDesc{};
	if (!Util::GetTexture2DDesc(source, sourceDesc) || !Util::GetTexture2DDesc(target ? static_cast<ID3D11View*>(target) : depth, destDesc))
		return;
	ResolveConstants constants{
		{ float(sourceDesc.Width), float(sourceDesc.Height) },
		{ float(destDesc.Width), float(destDesc.Height) },
		dejitter ? globals::features::upscaling.jitter : float2{ 0, 0 },
		eyeCount, 0, interpolate ? 1u : 0u
	};
	// Jitter is measured in main-scene pixels, including for half-resolution masks.
	constants.jitter.x *= float(sourceDesc.Width) / renderWidth;
	constants.jitter.y *= float(sourceDesc.Height) / renderHeight;
	context->UpdateSubresource(resolveCB.get(), 0, nullptr, &constants, 0, 0);
	auto* cb = resolveCB.get();
	context->PSSetConstantBuffers(1, 1, &cb);
	ID3D11ShaderResourceView* srvs[] = { source, stencil };
	context->PSSetShaderResources(0, 2, srvs);
	auto* sampler = linearSampler.get();
	context->PSSetSamplers(0, 1, &sampler);
	D3D11_VIEWPORT viewport{ 0, 0, float(destDesc.Width), float(destDesc.Height), 0, 1 };
	context->RSSetViewports(1, &viewport);
	context->OMSetRenderTargets(target ? 1 : 0, target ? &target : nullptr, depth);
	const bool combinedDepthStencil = depth && stencil && depthStencilPS;
	context->OMSetDepthStencilState(combinedDepthStencil ? depthStencilState.get() : (depth ? depthState.get() : nullptr), 0);
	context->PSSetShader(combinedDepthStencil ? depthStencilPS.get() : (depth ? depthPS.get() : colorPS.get()), nullptr, 0);
	context->Draw(3, 0);
	if (depth && stencil && !combinedDepthStencil) {
		context->ClearDepthStencilView(depth, D3D11_CLEAR_STENCIL, 0, 0);
		context->PSSetShader(stencilPS.get(), nullptr, 0);
		for (uint32_t bit = 0; bit < 8; ++bit) {
			constants.stencilBit = 1u << bit;
			context->UpdateSubresource(resolveCB.get(), 0, nullptr, &constants, 0, 0);
			context->OMSetDepthStencilState(stencilStates[bit].get(), constants.stencilBit);
			context->Draw(3, 0);
		}
	}
	ID3D11ShaderResourceView* nulls[2]{};
	context->PSSetShaderResources(0, 2, nulls);
	context->OMSetRenderTargets(0, nullptr, nullptr);
}

void PerfMode::PrepareOutput()
{
	if (!sceneActive)
		return;
	CS_GPU_PASS("SceneResolution::SpatialFallback");
	Util::FullscreenPassScope scope(globals::d3d::context);
	auto* context = globals::d3d::context;
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(fullscreenVS.get(), nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->RSSetState(nullptr);
	context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	DrawResolve(Util::AsReal(globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV), nullptr, Util::AsReal(output.RTV), nullptr, true, true);
	context->CopyResource(Util::AsReal(sharpen.texture), Util::AsReal(output.texture));
}

void PerfMode::ResolveScene()
{
	if (!sceneActive)
		return;
	CS_GPU_PASS("SceneResolution::Resolve");
	SetSceneTargets(false);
	auto* context = globals::d3d::context;
	{
		Util::FullscreenPassScope scope(context);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(fullscreenVS.get(), nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->RSSetState(nullptr);
		context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		for (auto& target : colors) {
			if (target.slot == RE::RENDER_TARGETS::kMAIN) {
				context->CopyResource(Util::AsReal(target.native.texture), Util::AsReal(output.texture));
			} else if (target.slot == RE::RENDER_TARGETS::kNORMAL_TAAMASK_SSRMASK ||
					   target.slot == RE::RENDER_TARGETS::kNORMAL_TAAMASK_SSRMASK_SWAP ||
					   target.slot == RE::RENDER_TARGETS::kNORMAL_TAAMASK_SSRMASK_DOWNSAMPLED ||
					   target.slot == RE::RENDER_TARGETS::kMOTION_VECTOR ||
					   target.slot == RE::RENDER_TARGETS::kREFRACTION_NORMALS ||
					   target.slot == RE::RENDER_TARGETS::kUNDERWATER_MASK ||
					   target.slot == RE::RENDER_TARGETS::kTEMPORAL_AA_MASK) {
				DrawResolve(Util::AsReal(target.scene.SRV), nullptr, Util::AsReal(target.native.RTV), nullptr, false, true);
			}
		}
		for (auto& target : depths) {
			if (target.slot == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY)
				continue;
			DrawResolve(Util::AsReal(target.scene.depthSRV),
				target.slot == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN ? Util::AsReal(target.scene.stencilSRV) : nullptr,
				nullptr, Util::AsReal(target.native.views[0]), false, true);
		}
		auto& ds = globals::game::renderer->GetDepthStencilData().depthStencils;
		context->CopyResource(Util::AsReal(ds[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY].texture), Util::AsReal(ds[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture));
	}
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET, RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
}
