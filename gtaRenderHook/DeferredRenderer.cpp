// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "stdafx.h"
#include "DeferredRenderer.h"
#include "RwRenderEngine.h"
#include "RwD3D1XEngine.h"
#include "D3DRenderer.h"
#include "SAIdleHook.h"
#include "D3D1XShader.h"
#include "D3D1XTexture.h"
#include "ShadowRenderer.h"
#include "D3D1XIm2DPipeline.h"
#include "FullscreenQuad.h"
#include "HDRTonemapping.h"
#include "D3D1XStateManager.h"
#include "LightManager.h"
#include "CubemapReflectionRenderer.h"
#include "D3D1XShaderDefines.h"
#include "VolumetricLighting.h"
#include "AmbientOcclusion.h"
#include <game_sa\CScene.h>
#include <game_sa\CGame.h>
/// voxel stuff
struct CB_Raycasting {
	RwMatrix m_QuadToVoxel;
	RwMatrix m_VoxelToScreen;
};
DeferredSettingsBlock gDeferredSettings;
///
extern UINT m_uiDeferredStage = 0;
CDeferredRenderer::CDeferredRenderer()
{
	m_aDeferredRasters[0] = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT1555);
	m_aDeferredRasters[1] = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT1555);
	m_aDeferredRasters[2] = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE);
	m_pReflectionRaster = RwRasterCreate((int)(RsGlobal.maximumWidth*gDeferredSettings.SSRScale),(int)(RsGlobal.maximumHeight*gDeferredSettings.SSRScale), 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT1555);
	m_pLightingRaster = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE| rwRASTERFORMAT1555);
	// We use 2 final rasters to have one for rendering effects that use full lighted image texture for example SSR
	m_pFinalRasters[0] = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT1555);
	m_pFinalRasters[1] = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT1555);
	m_pFinalRasters[2] = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT1555);
	m_pFinalRasters[3] = RwRasterCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT1555);
	
	m_pSunLightingPS	= new CD3D1XPixelShader("shaders/Deferred.hlsl", "SunLightingPS", gDeferredSettings.m_pShaderDefineList);
	m_pPointLightingPS	= new CD3D1XPixelShader("shaders/Deferred.hlsl", "PointLightingPS", gDeferredSettings.m_pShaderDefineList);
	m_pFinalPassPS		= new CD3D1XPixelShader("shaders/Deferred.hlsl", "FinalPassPS", gDeferredSettings.m_pShaderDefineList);
	m_pBlitPassPS		= new CD3D1XPixelShader("shaders/Deferred.hlsl", "BlitPS");
	m_pAtmospherePassPS = new CD3D1XPixelShader("shaders/AtmosphericScattering.hlsl", "AtmosphericScatteringPS", gDeferredSettings.m_pShaderDefineList);
	m_pReflectionPassPS = new CD3D1XPixelShader("shaders/ScreenSpaceReflections.hlsl", "ReflectionPassPS", gDeferredSettings.m_pShaderDefineList);

	gDeferredSettings.m_aShaderPointers.push_back(m_pSunLightingPS);
	gDeferredSettings.m_aShaderPointers.push_back(m_pPointLightingPS);
	gDeferredSettings.m_aShaderPointers.push_back(m_pFinalPassPS);
	gDeferredSettings.m_aShaderPointers.push_back(m_pAtmospherePassPS);
	gDeferredSettings.m_aShaderPointers.push_back(m_pReflectionPassPS);
	for (int i = 0; i < 3; i++)
	{
		gDebugSettings.DebugRenderTargetList.push_back(m_aDeferredRasters[i]);
	}
	gDebugSettings.DebugRenderTargetList.push_back(m_pLightingRaster);
	gDebugSettings.DebugRenderTargetList.push_back(m_pReflectionRaster);

	m_pShadowRenderer	= new CShadowRenderer();
	m_pReflRenderer = new CCubemapReflectionRenderer(gDeferredSettings.CubemapSize);
	m_pTonemapping		= new CHDRTonemapping();
	m_pDeferredBuffer = new CD3D1XConstantBuffer<CBDeferredRendering>();
	m_pDeferredBuffer->SetDebugName("DeferredCB");
}


CDeferredRenderer::~CDeferredRenderer()
{
	delete m_pReflRenderer;
	delete m_pDeferredBuffer;
	delete m_pTonemapping;
	delete m_pShadowRenderer;
	delete m_pReflectionPassPS;
	delete m_pAtmospherePassPS;
	delete m_pSunLightingPS;
	delete m_pPointLightingPS;
	delete m_pFinalPassPS;
	delete m_pBlitPassPS;
	RwRasterDestroy(m_pReflectionRaster);
	RwRasterDestroy(m_pFinalRasters[3]);
	RwRasterDestroy(m_pFinalRasters[2]);
	RwRasterDestroy(m_pFinalRasters[1]);
	RwRasterDestroy(m_pFinalRasters[0]);
	RwRasterDestroy(m_pLightingRaster);
	RwRasterDestroy(m_aDeferredRasters[2]);
	RwRasterDestroy(m_aDeferredRasters[1]);
	RwRasterDestroy(m_aDeferredRasters[0]);
}

void CDeferredRenderer::RenderToGBuffer(void(*renderCB)())
{
	g_pDebug->printMsg("Rendering event GBuffer: start",1);
	CRwD3D1XEngine* dxEngine = (CRwD3D1XEngine*)g_pRwCustomEngine;

	CD3DRenderer*		renderer	= dxEngine->getRenderer();
	
	m_uiDeferredStage = 1;

	renderer->BeginDebugEvent(L"GBuffer pass");
	g_pRwCustomEngine->SetRenderTargets(m_aDeferredRasters, Scene.m_pRwCamera->zBuffer, 3);
	renderCB();
	g_pRwCustomEngine->SetRenderTargets(&Scene.m_pRwCamera->frameBuffer, Scene.m_pRwCamera->zBuffer, 1);
	
	renderer->EndDebugEvent();
	g_pDebug->printMsg("Rendering event GBuffer: end", 1);
}

void CDeferredRenderer::RenderOutput()
{

	//g_pDebug->printMsg("Start of deffered output.");
	m_pDeferredBuffer->data.SSRMaxIterations = gDeferredSettings.SSRMaxIterations;
	m_pDeferredBuffer->data.SSRStep = gDeferredSettings.SSRStep;
	m_pDeferredBuffer->data.MaxShadowBlur = gDeferredSettings.MaxShadowBlur;
	m_pDeferredBuffer->data.MinShadowBlur = gDeferredSettings.MinShadowBlur;
	m_pDeferredBuffer->Update();
	g_pStateMgr->SetConstantBufferPS(m_pDeferredBuffer, 6);
	CAmbientOcclusion::RenderAO(m_aDeferredRasters[1]);
	g_pRwCustomEngine->SetRenderTargets(&m_pLightingRaster, Scene.m_pRwCamera->zBuffer, 1);
	g_pStateMgr->FlushRenderTargets();
	// Set deferred textures
	g_pStateMgr->SetRaster(m_aDeferredRasters[0]);
	for (auto i = 1; i < 3; i++)
		g_pStateMgr->SetRaster(m_aDeferredRasters[i], i);
	
	m_pShadowRenderer->SetShadowBuffer();

	// Render sun directional light if required
	if (g_shaderRenderStateBuffer.vSunDir.w > 0&&CGame::currArea==0) 
	{
		m_pSunLightingPS->Set();
		g_pDebug->printMsg("Sun light rendering.", 1);
		CFullscreenQuad::Draw();
	}

	// Render point and spot lights
	if (CLightManager::m_nLightCount > 0) {
		g_pStateMgr->SetStructuredBufferPS(CLightManager::GetBuffer(), 5);
		g_pRwCustomEngine->RenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, TRUE);
		g_pRwCustomEngine->RenderStateSet(rwRENDERSTATESRCBLEND, rwBLENDONE);
		g_pRwCustomEngine->RenderStateSet(rwRENDERSTATEDESTBLEND, rwBLENDONE);
		g_pDebug->printMsg("Point light rendering.", 1);
		m_pPointLightingPS->Set();
		CFullscreenQuad::Draw();
	}
	
	g_pRwCustomEngine->RenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, FALSE);
	g_pRwCustomEngine->RenderStateSet(rwRENDERSTATESRCBLEND, rwBLENDSRCALPHA);
	g_pRwCustomEngine->RenderStateSet(rwRENDERSTATEDESTBLEND, rwBLENDINVSRCALPHA);

	// Render reflection pass
	g_pRwCustomEngine->SetRenderTargets(&m_pReflectionRaster, Scene.m_pRwCamera->zBuffer, 1);
	g_pStateMgr->FlushRenderTargets();
	g_pStateMgr->SetRaster(m_pFinalRasters[2], 3);
	m_pReflRenderer->SetCubemap();
	m_pReflectionPassPS->Set();
	CFullscreenQuad::Draw();

	// Compose lighting with color and reflections.
	g_pRwCustomEngine->SetRenderTargets(&m_pFinalRasters[m_uiCurrentFinalRaster], Scene.m_pRwCamera->zBuffer, 1);
	g_pStateMgr->FlushRenderTargets();
	g_pStateMgr->SetRaster(m_pLightingRaster, 4);
	g_pStateMgr->SetRaster(m_pFinalRasters[1 - m_uiCurrentFinalRaster], 5);
	g_pStateMgr->SetRaster(m_pReflectionRaster, 6);
	g_pStateMgr->SetRaster(CAmbientOcclusion::GetAORaster(), 7);
	
	m_pFinalPassPS->Set(); 
	CFullscreenQuad::Draw();

	for (auto i = 0; i < 7; i++)
		g_pStateMgr->SetRaster(nullptr, i);

	// Atmospheric scattering
	g_pRwCustomEngine->SetRenderTargets(&m_pFinalRasters[2], Scene.m_pRwCamera->zBuffer, 1);
	g_pStateMgr->FlushRenderTargets();
	g_pStateMgr->SetRaster(m_pFinalRasters[m_uiCurrentFinalRaster], 0);
	g_pStateMgr->SetRaster(m_aDeferredRasters[1], 1);
	//m_pShadowRenderer->SetShadowBuffer();
	m_pAtmospherePassPS->Set();
	CFullscreenQuad::Draw();
	
	for (auto i = 0; i < 7; i++)
		g_pStateMgr->SetRaster(nullptr, i);

	// Restore texture 
	g_pRwCustomEngine->SetRenderTargets(&m_pFinalRasters[m_uiCurrentFinalRaster], Scene.m_pRwCamera->zBuffer, 1);
	g_pStateMgr->FlushRenderTargets();

	g_pStateMgr->SetRaster(m_pFinalRasters[2], 5);
	m_pBlitPassPS->Set();
	CFullscreenQuad::Draw();

	for (auto i = 0; i < 7; i++)
		g_pStateMgr->SetRaster(nullptr, i);
}

void CDeferredRenderer::RenderToCubemap(void(*renderCB)())
{
	m_pReflRenderer->RenderToCubemap(renderCB);
}

void CDeferredRenderer::RenderTonemappedOutput()
{
	CVolumetricLighting::RenderVolumetricEffects(m_aDeferredRasters[1],
		m_pShadowRenderer->m_pShadowCamera->zBuffer, m_pFinalRasters[m_uiCurrentFinalRaster]);
	m_pTonemapping->Render(m_pFinalRasters[m_uiCurrentFinalRaster]);
	m_uiCurrentFinalRaster = 1-m_uiCurrentFinalRaster;
	for (auto i = 0; i < 7; i++)
		g_pStateMgr->SetRaster(nullptr, i);
}

void CDeferredRenderer::SetNormalDepthRaster()
{
	g_pStateMgr->SetRaster(m_aDeferredRasters[1], 1);
}

void CDeferredRenderer::SetPreviousFinalRaster()
{
	g_pStateMgr->SetRaster(m_pFinalRasters[1 - m_uiCurrentFinalRaster], 2);
}

void CDeferredRenderer::SetPreviousNonTonemappedFinalRaster()
{
	g_pStateMgr->SetRaster(m_pFinalRasters[2], 2);
}

void CDeferredRenderer::QueueTextureReload()
{
	CRwD3D1XEngine* dxEngine = (CRwD3D1XEngine*)g_pRwCustomEngine;

	m_pShadowRenderer->QueueTextureReload();
	
	if (m_bRequiresReloading || dxEngine->m_bScreenSizeChanged) {
		m_pReflectionRaster->width = (int)(RsGlobal.maximumWidth*gDeferredSettings.SSRScale);
		m_pReflectionRaster->height = (int)(RsGlobal.maximumHeight*gDeferredSettings.SSRScale);
		dxEngine->m_pRastersToReload.push_back(m_pReflectionRaster);
	}

	if (dxEngine->m_bScreenSizeChanged) {
		for (int i = 0; i < 3; i++) {
			m_aDeferredRasters[i]->width = RsGlobal.maximumWidth;
			m_aDeferredRasters[i]->height = RsGlobal.maximumHeight;

			dxEngine->m_pRastersToReload.push_back(m_aDeferredRasters[i]);
		}
		m_pLightingRaster->width = RsGlobal.maximumWidth;
		m_pLightingRaster->height = RsGlobal.maximumHeight;
		dxEngine->m_pRastersToReload.push_back(m_pLightingRaster);

		
		for (int i = 0; i < 4; i++) {
			m_pFinalRasters[i]->width = RsGlobal.maximumWidth;
			m_pFinalRasters[i]->height = RsGlobal.maximumHeight;
			dxEngine->m_pRastersToReload.push_back(m_pFinalRasters[i]);
		}
	}
	
}

tinyxml2::XMLElement * DeferredSettingsBlock::Save(tinyxml2::XMLDocument * doc)
{
	auto deferredSettingsNode = doc->NewElement(m_sName.c_str());
	deferredSettingsNode->SetAttribute("MaxReflectionIterations", SSRMaxIterations);
	deferredSettingsNode->SetAttribute("ReflectionStep", SSRStep);
	deferredSettingsNode->SetAttribute("ReflectionScale", SSRScale);
	deferredSettingsNode->SetAttribute("MaxShadowBlur", MaxShadowBlur);
	deferredSettingsNode->SetAttribute("MinShadowBlur", MinShadowBlur);
	deferredSettingsNode->SetAttribute("ShadowsBlurKernelSize", ShadowsBlurKernelSize);
	deferredSettingsNode->SetAttribute("BlurShadows", BlurShadows);
	deferredSettingsNode->SetAttribute("UsePCSS", UsePCSS);
	deferredSettingsNode->SetAttribute("SampleShadows", SampleShadows);
	deferredSettingsNode->SetAttribute("UseSSR", UseSSR);
	deferredSettingsNode->SetAttribute("SampleCubemap", SampleCubemap);
	deferredSettingsNode->SetAttribute("CubemapSize", CubemapSize);
	return deferredSettingsNode;
}

void DeferredSettingsBlock::Load(const tinyxml2::XMLDocument & doc)
{
	auto deferredSettingsNode = doc.FirstChildElement(m_sName.c_str());
	if (deferredSettingsNode == nullptr) {
		Reset();
		return;
	}
	// Debug
	SSRMaxIterations = (unsigned int)deferredSettingsNode->IntAttribute("MaxReflectionIterations", 24);
	SSRStep = deferredSettingsNode->FloatAttribute("ReflectionStep", 0.0025f);
	SSRScale = deferredSettingsNode->FloatAttribute("ReflectionScale", 0.75f);
	MaxShadowBlur = deferredSettingsNode->FloatAttribute("MaxShadowBlur", 4.0f);
	MinShadowBlur = deferredSettingsNode->FloatAttribute("MinShadowBlur", 0.3f);
	ShadowsBlurKernelSize = deferredSettingsNode->IntAttribute("ShadowsBlurKernelSize", 2);
	BlurShadows = deferredSettingsNode->BoolAttribute("BlurShadows", true);
	UsePCSS = deferredSettingsNode->BoolAttribute("UsePCSS", true);
	SampleShadows = deferredSettingsNode->BoolAttribute("SampleShadows", true);
	UseSSR = deferredSettingsNode->BoolAttribute("UseSSR", true);
	SampleCubemap = deferredSettingsNode->BoolAttribute("SampleCubemap", true);
	CubemapSize = deferredSettingsNode->IntAttribute("CubemapSize", 128);

	gDeferredSettings.m_pShaderDefineList = new CD3D1XShaderDefineList();
	gDeferredSettings.m_pShaderDefineList->AddDefine("SSR_SAMPLE_COUNT", to_string(SSRMaxIterations));
	gDeferredSettings.m_pShaderDefineList->AddDefine("SAMPLE_SHADOWS", to_string((int)SampleShadows));
	gDeferredSettings.m_pShaderDefineList->AddDefine("BLUR_SHADOWS", to_string((int)BlurShadows));
	gDeferredSettings.m_pShaderDefineList->AddDefine("USE_PCS_SHADOWS", to_string((int)UsePCSS));
	gDeferredSettings.m_pShaderDefineList->AddDefine("SHADOW_BLUR_KERNEL", to_string(ShadowsBlurKernelSize));
	gDeferredSettings.m_pShaderDefineList->AddDefine("USE_SSR", to_string((int)gDeferredSettings.UseSSR));
	gDeferredSettings.m_pShaderDefineList->AddDefine("SAMPLE_CUBEMAP", to_string((int)gDeferredSettings.SampleCubemap));
}

void DeferredSettingsBlock::Reset()
{
	SSRMaxIterations = 24;
	SSRStep = 0.0025f;
	MaxShadowBlur = 4.0f;
	MinShadowBlur = 0.3f;
	SSRScale = 0.75f;
	ShadowsBlurKernelSize = 2;
	BlurShadows = true;
	UsePCSS = true;
	SampleShadows = true;
	CubemapSize = 128;
	UseSSR = true;
	SampleCubemap = true;
}
void TW_CALL ReloadDeferredShadersCallBack(void *value)
{
	gDeferredSettings.m_bShaderReloadRequired = true;
	gDeferredSettings.m_pShaderDefineList->Reset();
	gDeferredSettings.m_pShaderDefineList->AddDefine("SSR_SAMPLE_COUNT", to_string(gDeferredSettings.SSRMaxIterations));
	gDeferredSettings.m_pShaderDefineList->AddDefine("SAMPLE_SHADOWS", to_string((int)gDeferredSettings.SampleShadows));
	gDeferredSettings.m_pShaderDefineList->AddDefine("BLUR_SHADOWS", to_string((int)gDeferredSettings.BlurShadows));
	gDeferredSettings.m_pShaderDefineList->AddDefine("USE_PCS_SHADOWS", to_string((int)gDeferredSettings.UsePCSS));
	gDeferredSettings.m_pShaderDefineList->AddDefine("SHADOW_BLUR_KERNEL", to_string(gDeferredSettings.ShadowsBlurKernelSize));
	gDeferredSettings.m_pShaderDefineList->AddDefine("USE_SSR", to_string((int)gDeferredSettings.UseSSR));
	gDeferredSettings.m_pShaderDefineList->AddDefine("SAMPLE_CUBEMAP", to_string((int)gDeferredSettings.SampleCubemap));
}
void TW_CALL ReloadDeferredTexturesCallBack(void *value)
{
	g_pDeferredRenderer->m_bRequiresReloading = true;
}
void DeferredSettingsBlock::InitGUI(TwBar * bar)
{
	// Deferred shadow settings
	TwAddVarRW(bar, "Use Blurring",							TwType::TW_TYPE_BOOL8, &BlurShadows, "group=ShadowBlur");
	TwAddVarRW(bar, "Shadow Blur Kernel Size",				TwType::TW_TYPE_UINT32, &ShadowsBlurKernelSize, "min=1 max=16 step=1 group=ShadowBlur");
	TwAddVarRW(bar, "Max Shadow Blur",						TwType::TW_TYPE_FLOAT, &MaxShadowBlur, " min=0 max=10 step=0.005 help='meh' group=ShadowBlur");
	TwAddVarRW(bar, "Min Shadow Blur",						TwType::TW_TYPE_FLOAT, &MinShadowBlur, " min=0 max=10 step=0.005 help='meh' group=ShadowBlur");
	TwAddVarRW(bar, "Use Percentage Closer Soft Shadows",	TwType::TW_TYPE_BOOL8, &UsePCSS, "group=ShadowBlur");
	TwAddVarRW(bar, "Sample Shadows",						TwType::TW_TYPE_BOOL8, &SampleShadows, "group=ShadowBlur");
	
	TwDefine("Settings/ShadowBlur   group=Deferred label='Shadow bluring'");

	TwAddVarRW(bar, "Sample Cubemap", TwType::TW_TYPE_BOOL8, &SampleCubemap, "group=Reflections");

	TwDefine("Settings/Reflections   group=Deferred label='Reflections'");
	// Deferred reflections settings
	TwAddVarRW(bar, "Maximum iterations", TwType::TW_TYPE_UINT32, &SSRMaxIterations, " min=2 max=512 step=1 help='meh' group=SSR");
	TwAddVarRW(bar, "Step", TwType::TW_TYPE_FLOAT, &SSRStep, " min=0 max=10 step=0.0001 help='meh' group=SSR");
	TwAddVarRW(bar, "Reflection scaling", TwType::TW_TYPE_FLOAT, &SSRScale, "min=0.1 max=1.5 step=0.01 group=SSR");
	TwAddVarRW(bar, "Use SSR", TwType::TW_TYPE_BOOL8, &UseSSR, "group=Reflections");
	TwDefine("Settings/SSR   group=Deferred label='Screen-Space Reflection'");

	TwAddButton(bar, "Reload shaders", ReloadDeferredShadersCallBack, nullptr, "group=Deferred");
	TwAddButton(bar, "Reload textures", ReloadDeferredTexturesCallBack, nullptr, "group=Deferred");
}
