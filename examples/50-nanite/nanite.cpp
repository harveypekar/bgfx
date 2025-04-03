/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "common.h"
#include "bgfx/bgfx.h"
#include "bgfx_utils.h"


#include <bx/allocator.h>
#include <bx/file.h>
#include <bx/string.h>

#include "imgui/imgui.h"
#include <entry/input.h>

#include <iostream>
#include <format>
#include <chrono>

namespace
{

struct PosTexCoord0Vertex
{
	float m_x;
	float m_y;
	float m_z;
	float m_u;
	float m_v;

	static void init()
	{
		ms_layout
			.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
	}

	static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosTexCoord0Vertex::ms_layout;

#define REFERENCE_SAMPLES 64

class ExampleNanite : public entry::AppI
{
public:
	ExampleNanite(const char* _name, const char* _description, const char* _url)
		: entry::AppI(_name, _description, _url)
		, m_camPos(0.0f,0.0f,0.0f)
		, m_bunnyPos(0.0f, 0.0f, 2.0f)
		, m_fov(0.0f)
	{
	}

	void init(int32_t _argc, const char* const* _argv, uint32_t _width, uint32_t _height) override
	{
		std::time_t t = std::time(nullptr);
		std::tm* const pTInfo = std::localtime(&t);

		int year = 1900 + pTInfo->tm_year;
		int month = pTInfo->tm_mon + 1;
		int day = pTInfo->tm_mday;
		int hour = pTInfo->tm_hour;
		int minutes = pTInfo->tm_min;
		int seconds = pTInfo->tm_sec;

		m_runPrefix = (char*)malloc(1024);
		bx::snprintf(m_runPrefix, 1024,
			"%d_%d_%d_%d_%d_%d",
			year,
			month,
			day,
			hour,
			minutes,
			seconds
			);


		Args args(_argc, _argv);

		m_width = _width;
		m_height = _height;
		m_debug = BGFX_DEBUG_NONE;
		m_reset = BGFX_RESET_VSYNC;

		bgfx::Init init;
		init.type = bgfx::RendererType::Direct3D12;
		init.vendorId = args.m_pciId;
		init.platformData.nwh = entry::getNativeWindowHandle(entry::kDefaultWindowHandle);
		init.platformData.ndt = entry::getNativeDisplayHandle();
		init.platformData.type = entry::getNativeWindowHandleType();
		init.resolution.width = m_width;
		init.resolution.height = m_height;
		init.resolution.reset = m_reset;
		bgfx::init(init);

		m_frameNum = 0;

		// Enable debug text.
		bgfx::setDebug(m_debug);

		// Set view 0 clear state.
		bgfx::setViewClear(kRenderPassBaseGeometry
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x000000FF
			, 1.0f
			, 0
		);

		// Set light pass view clear state.
		bgfx::setViewClear(kRenderPassNaniteGeometry
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x000000FF
			, 1.0f
			, 0
		);


		// Set geometry pass view clear state.
		bgfx::setViewClear(kRenderPassSuperGeometry
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x000000FF
			, 1.0f
			, 0
		);

		// Set geometry pass view clear state.
		bgfx::setViewClear(kRenderPassSuperAccumulate
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x000000FF
			, 1.0f
			, 0
		);

		// Set geometry pass view clear state.
		bgfx::setViewClear(kRenderPassCombine
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x000000FF
			, 1.0f
			, 0
		);

		u_time = bgfx::createUniform("u_time", bgfx::UniformType::Vec4);

		PosTexCoord0Vertex::init();

		// Create program from shaders.
		m_program = loadProgram("vs_50_mesh", "fs_50_mesh");
		m_displayProgram = loadProgram("vs_50_compost", "fs_50_compost");

		m_mesh = meshLoad("meshes/bunny.bin");


		
		const uint64_t bilinearFlags = 0
			| BGFX_TEXTURE_RT
			| BGFX_SAMPLER_U_CLAMP
			| BGFX_SAMPLER_V_CLAMP
			;


		bgfx::setViewFrameBuffer(kRenderPassCombine, BGFX_INVALID_HANDLE);
		bgfx::setViewName(kRenderPassCombine, "ViewCombine");

		bgfx::TextureFormat::Enum depthFormat =
			bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT)
			? bgfx::TextureFormat::D32F
			: bgfx::TextureFormat::D24
			;


		m_baseRT = bgfx::createTexture2D(
			uint16_t(m_width), uint16_t(m_height), false, 1, bgfx::TextureFormat::RGBA8, bilinearFlags);
		bgfx::setName(m_baseRT, "baseRT");
		m_baseDepthRT = bgfx::createTexture2D(uint16_t(m_width), uint16_t(m_height), false, 1, depthFormat, BGFX_TEXTURE_RT);
		bgfx::setName(m_baseDepthRT, "baseDepthRT");
		bgfx::Attachment baseAttachments[2];
		baseAttachments[0].init(m_baseRT);
		baseAttachments[1].init(m_baseDepthRT);
		m_baseFB = bgfx::createFrameBuffer(2, baseAttachments);
		bgfx::setName(m_baseFB, "baseFB");
		bgfx::setViewFrameBuffer(kRenderPassBaseGeometry, m_baseFB);
		bgfx::setViewName(kRenderPassBaseGeometry, "ViewBaseGeometry");

		m_baseRB = bgfx::createTexture2D(
			uint16_t(m_width), uint16_t(m_height), false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
		setName(m_baseRB, "baseRB");

		m_naniteRT = bgfx::createTexture2D(
			uint16_t(m_width), uint16_t(m_height), false, 1, bgfx::TextureFormat::RGBA8, bilinearFlags);
		m_naniteDepthRT = bgfx::createTexture2D(uint16_t(m_width), uint16_t(m_height), false, 1, depthFormat, BGFX_TEXTURE_RT);
		bgfx::Attachment naniteAttachments[2];
		naniteAttachments[0].init(m_naniteRT);
		naniteAttachments[1].init(m_naniteDepthRT);
		m_naniteFB = bgfx::createFrameBuffer(2, naniteAttachments);
		bgfx::setViewFrameBuffer(kRenderPassNaniteGeometry, m_naniteFB);
		bgfx::setViewName(kRenderPassNaniteGeometry, "ViewNaniteGeometry");

		m_naniteRB = bgfx::createTexture2D(
			uint16_t(m_width), uint16_t(m_height), false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
		setName(m_naniteRB, "naniteRB");

		m_superRT = bgfx::createTexture2D(
			uint16_t(m_width), uint16_t(m_height), false, 1, bgfx::TextureFormat::RGBA8, bilinearFlags);
		m_superDepthRT = bgfx::createTexture2D(uint16_t(m_width), uint16_t(m_height), false, 1, depthFormat, BGFX_TEXTURE_RT);

		bgfx::Attachment superAttachments[2];
		superAttachments[0].init(m_superRT);
		superAttachments[1].init(m_superDepthRT);
		m_superFB = bgfx::createFrameBuffer(2, superAttachments);
		bgfx::setViewFrameBuffer(kRenderPassSuperGeometry, m_superFB);
		bgfx::setViewName(kRenderPassSuperGeometry, "ViewSuperGeometry");

		m_naniteRB = bgfx::createTexture2D(
			uint16_t(m_width), uint16_t(m_height), false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
		setName(m_naniteRB, "naniteRB");

		m_samplerBase = bgfx::createUniform("samplerBase", bgfx::UniformType::Sampler);
		m_samplerNanite = bgfx::createUniform("samplerNanite", bgfx::UniformType::Sampler);

		m_baseRBTimer = 0xDEADBEEF;
		m_naniteRBTimer = 0xDEADBEEF;
		m_baseRBReady = false;
		m_naniteRBReady = false;

		m_basePixelBuffer = (uint8_t*)malloc(sizeof(uint32_t) * m_width * m_height);
		m_nanitePixelBuffer = (uint8_t*)malloc(sizeof(uint32_t) * m_width * m_height);

		m_timeOffset = bx::getHPCounter();

		imguiCreate();
	}

	int shutdown() override
	{
		imguiDestroy();

		meshUnload(m_mesh);

		// Cleanup.
		bgfx::destroy(m_program);
		bgfx::destroy(m_displayProgram);

		bgfx::destroy(u_time);

		// Shutdown bgfx.
		bgfx::shutdown();

		return 0;
	}


	void screenSpaceQuad(bool _originBottomLeft, float _width = 1.0f, float _height = 1.0f)
	{
		if (3 == bgfx::getAvailTransientVertexBuffer(3, PosTexCoord0Vertex::ms_layout))
		{
			bgfx::TransientVertexBuffer vb;
			bgfx::allocTransientVertexBuffer(&vb, 3, PosTexCoord0Vertex::ms_layout);
			PosTexCoord0Vertex* vertex = (PosTexCoord0Vertex*)vb.data;

			const float minx = -_width;
			const float maxx = _width;
			const float miny = 0.0f;
			const float maxy = _height * 2.0f;

			const float minu = -1.0f;
			const float maxu = 1.0f;

			const float zz = 0.0f;

			float minv = 0.0f;
			float maxv = 2.0f;

			if (_originBottomLeft)
			{
				float temp = minv;
				minv = maxv;
				maxv = temp;

				minv -= 1.0f;
				maxv -= 1.0f;
			}

			vertex[0].m_x = minx;
			vertex[0].m_y = miny;
			vertex[0].m_z = zz;
			vertex[0].m_u = minu;
			vertex[0].m_v = minv;

			vertex[1].m_x = maxx;
			vertex[1].m_y = miny;
			vertex[1].m_z = zz;
			vertex[1].m_u = maxu;
			vertex[1].m_v = minv;

			vertex[2].m_x = maxx;
			vertex[2].m_y = maxy;
			vertex[2].m_z = zz;
			vertex[2].m_u = maxu;
			vertex[2].m_v = maxv;

			bgfx::setVertexBuffer(0, &vb);
		}
	}

	void renderScene(bgfx::ViewId viewId)
	{
		
		float model[16];
		bx::mtxTranslate(model, m_bunnyPos.x, m_bunnyPos.y, m_bunnyPos.z);
		bx::mtxRotateXY(model, 0.0f, 0.0f);

		meshSubmit(m_mesh, viewId, m_program, model);
	}

	void updateViewMatrix()
	{
		const bgfx::Caps* caps = bgfx::getCaps();

		bx::mtxLookAt(m_view, m_camPos, m_bunnyPos);

		float nearF = .1f;
		float farF = 100.0f;

		const float aspect = float(m_width) / float(m_height);
		m_fov.y = 60;
		m_fov.x = m_fov.y * aspect;
		bx::mtxProj(m_proj, m_fov.y, aspect, nearF, farF, caps->homogeneousDepth);

	}

	void renderBase()
	{

		bgfx::setMarker("[DN] Base Pass");

		bgfx::setState(BGFX_STATE_MSAA, 0);
		bgfx::setViewRect(kRenderPassBaseGeometry, 0, 0, uint16_t(m_width), uint16_t(m_height));

		// This dummy draw call is here to make sure that view 0 is cleared
		// if no other draw calls are submitted to view 0.
		bgfx::touch(kRenderPassBaseGeometry);


		const bx::Vec3 at = bx::Vec3(0,0,1);

		// Set view and projection matrix for view 0.
		{
			bgfx::setViewTransform(kRenderPassBaseGeometry, m_view, m_proj);

			// Set view 0 default viewport.
			bgfx::setViewRect(kRenderPassBaseGeometry, 0, 0, uint16_t(m_width), uint16_t(m_height));
		}

		renderScene(kRenderPassBaseGeometry);
	}

	void renderNanite()
	{
		bgfx::setMarker("[DN] Nanite Pass");

		bgfx::setState(BGFX_STATE_MSAA, 1);

		// Set view 0 default viewport.
		bgfx::setViewRect(kRenderPassNaniteGeometry, 0, 0, uint16_t(m_width), uint16_t(m_height));

		// This dummy draw call is here to make sure that view 0 is cleared
		// if no other draw callGs are submitted to view 0.
		bgfx::touch(kRenderPassNaniteGeometry);

			bgfx::setViewTransform(kRenderPassNaniteGeometry, m_view, m_proj);

			// Set view 0 default viewport.
			bgfx::setViewRect(kRenderPassNaniteGeometry, 0, 0, uint16_t(m_width), uint16_t(m_height));
		
		renderScene(kRenderPassNaniteGeometry);
	}

	void renderReference()
	{
		bgfx::setMarker("[DN] Nanite Pass");
		bgfx::setState(BGFX_STATE_MSAA, 0);
		bgfx::setViewRect(kRenderPassSuperGeometry, 0, 0,
			uint16_t(m_width), uint16_t(m_height));
		bgfx::touch(kRenderPassSuperGeometry);




		bgfx::setViewTransform(kRenderPassSuperGeometry, m_view, m_proj);
		bgfx::setViewRect(kRenderPassSuperGeometry, 0, 0, uint16_t(m_width), uint16_t(m_height));

		renderScene(kRenderPassSuperGeometry);
	}


	bool update() override
	{
		if (!entry::processEvents(m_width, m_height, m_debug, m_reset, &m_mouseState))
		{

			int64_t currentTime = bx::getHPCounter();
			int64_t elapsedTime = currentTime - m_time;
			m_time = currentTime;

			float elapsedTimeF = (float)((elapsedTime) / double(bx::getHPFrequency()));
			float time = (float)((currentTime) / double(bx::getHPFrequency()));

			bgfx::setUniform(u_time, &time);
			/*
			*
			*
			imguiBeginFrame(m_mouseState.m_mx
				,  m_mouseState.m_my
				, (m_mouseState.m_buttons[entry::MouseButton::Left  ] ? IMGUI_MBUT_LEFT   : 0)
				| (m_mouseState.m_buttons[entry::MouseButton::Right ] ? IMGUI_MBUT_RIGHT  : 0)
				| (m_mouseState.m_buttons[entry::MouseButton::Middle] ? IMGUI_MBUT_MIDDLE : 0)
				,  m_mouseState.m_mz
				, uint16_t(m_width)
				, uint16_t(m_height)
				);

			showExampleDialog(this);


			ImGui::TextWrapped("pos %f %f %f", camPos.x, camPos.y, camPos.z);

			imguiEndFrame();
			*/

			bx::Vec3 translation = bx::Vec3(0.0f);
			if (inputGetKeyState(entry::Key::KeyW))
				translation = add(translation, bx::Vec3(0.0f, 0.0f, 1.0f));
			if (inputGetKeyState(entry::Key::KeyR))
				translation = add(translation, bx::Vec3(0.0f, 0.0f, -1.0f));
			if (inputGetKeyState(entry::Key::KeyS))
				translation = add(translation, bx::Vec3(1.0f, 0.0f, 0.0f));
			if (inputGetKeyState(entry::Key::KeyA))
				translation = add(translation, bx::Vec3(-1.0f, 0.0f, 0.0));
			if (inputGetKeyState(entry::Key::KeyQ))
				translation = add(translation, bx::Vec3(0.0f, 1.0f, 0.0));
			if (inputGetKeyState(entry::Key::KeyF))
				translation = add(translation, bx::Vec3(0.0f, -1.0f, 0.0));
			
			translation = mul(normalize(translation), elapsedTimeF* 1.0f );

	
			m_camPos = add(m_camPos, translation);

			updateViewMatrix();

			renderBase();
			renderNanite();

			if (inputGetKeyState(entry::Key::KeyB) && !m_baseRBReady && !m_naniteRBReady)
			{
				bgfx::setMarker("[DN] Readback Pass");

				m_baseRBReady = true;
				bgfx::blit(kRenderPassCopyBack, m_baseRB, 0, 0, m_baseRT);
				m_baseRBTimer = bgfx::readTexture(m_baseRB, m_basePixelBuffer);


				m_naniteRBReady = true;
				bgfx::blit(kRenderPassCopyBack, m_naniteRB, 0, 0, m_naniteRT);
				m_naniteRBTimer = bgfx::readTexture(m_naniteRB, m_nanitePixelBuffer);
			}

			if (m_frameNum == m_baseRBTimer + 2)
			{
				bx::FileWriter writer;
				char nameBuffer[1024];
				snprintf(nameBuffer, 1024, "%s_%d_%s.png", m_runPrefix,
					m_baseRBTimer, "base");
				if (bx::open(&writer, nameBuffer, false, bx::ErrorAssert{}))
				{
					//bimg::imageWriteTga(&writer, m_width, m_height, m_width*sizeof(uint32_t), m_basePixelBuffer, false, false, bx::ErrorAssert{});
					bimg::imageWritePng(&writer, m_width, m_height, m_width * sizeof(uint32_t), m_basePixelBuffer, bimg::TextureFormat::RGBA8, false, bx::ErrorAssert{});
					bx::close(&writer);
				}

				m_baseRBReady = false;
			}

			if (m_frameNum == m_naniteRBTimer + 2)
			{
				bx::FileWriter writer;
				char nameBuffer[1024];
				snprintf(nameBuffer, 1024, "%s_%d_%s.png", m_runPrefix,
					m_naniteRBTimer, "nanite");
				if (bx::open(&writer, nameBuffer, false, bx::ErrorAssert{}))
				{
					//bimg::imageWriteTga(&writer, m_width, m_height, m_width*sizeof(uint32_t), m_basePixelBuffer, false, false, bx::ErrorAssert{});
					bimg::imageWritePng(&writer, m_width, m_height, m_width * sizeof(uint32_t), m_nanitePixelBuffer, bimg::TextureFormat::RGBA8, false, bx::ErrorAssert{});
					bx::close(&writer);
				}

				m_naniteRBReady = false;
			}

			float proj[16];
			const bgfx::Caps* caps = bgfx::getCaps();
			bx::mtxOrtho(proj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, caps->homogeneousDepth);
			bgfx::setViewTransform(kRenderPassCombine, NULL, proj);

			bgfx::setViewRect(kRenderPassCombine, 0, 0, uint16_t(m_width), uint16_t(m_height));


			bgfx::touch(kRenderPassCombine);
			bgfx::setMarker("[DN] Combine Pass");

			// Combine color and light buffers.
			bgfx::setTexture(0, m_samplerBase, m_baseRT);
			bgfx::setTexture(1, m_samplerNanite, m_naniteRT);
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
			);

			screenSpaceQuad(false);
			bgfx::submit(kRenderPassCombine, m_displayProgram);

			// Advance to next frame. Rendering thread will be kicked to
			// process submitted rendering primitives.
			m_frameNum = bgfx::frame();

			return true;
		}

		return false;
	}

	entry::MouseState m_mouseState;

	uint32_t m_width;
	uint32_t m_height;
	uint32_t m_debug;
	uint32_t m_reset;

	int64_t m_timeOffset;
	int64_t m_time;
	uint32_t m_frameNum;

	bx::Vec3 m_fov;

	Mesh* m_mesh;
	bgfx::ProgramHandle m_program;
	bgfx::ProgramHandle m_displayProgram;
	bgfx::UniformHandle u_time;

	bgfx::TextureHandle m_baseRT;
	bgfx::TextureHandle m_baseDepthRT;
	bgfx::FrameBufferHandle m_baseFB;
	bgfx::TextureHandle m_baseRB;

	bgfx::TextureHandle m_naniteRT;
	bgfx::TextureHandle m_naniteDepthRT;
	bgfx::FrameBufferHandle m_naniteFB;
	bgfx::TextureHandle m_naniteRB;


	bgfx::TextureHandle m_superRT;
	bgfx::TextureHandle m_superDepthRT;
	bgfx::FrameBufferHandle m_superFB;
	bgfx::TextureHandle m_superAccumRT;
	bgfx::TextureHandle m_superAccumFB;
	bgfx::TextureHandle m_superAccumRB;


	uint32_t m_baseRBTimer;
	uint32_t m_naniteRBTimer;
	uint32_t m_referenceRBTimer;
	bool m_baseRBReady;
	bool m_naniteRBReady;
	bool m_referenceRBReady;

	uint8_t* m_basePixelBuffer;
	uint8_t* m_nanitePixelBuffer;
	float* m_superPixelBuffer;


	bgfx::UniformHandle m_samplerBase;
	bgfx::UniformHandle m_samplerNanite;

	const bgfx::ViewId kRenderPassBaseGeometry = 1;
	const bgfx::ViewId kRenderPassNaniteGeometry = 2;
	const bgfx::ViewId kRenderPassSuperGeometry = 3;
	const bgfx::ViewId kRenderPassSuperAccumulate = 4;
	const bgfx::ViewId kRenderPassCopyBack = 5;
	const bgfx::ViewId kRenderPassCombine = 6;


	bx::Vec3 m_camPos;
	float m_view[16];
	float m_proj[16];

	bx::Vec3 m_bunnyPos;

	char* m_runPrefix;
};

} // namespace

ENTRY_IMPLEMENT_MAIN(
	ExampleNanite
	, "50-nanite"
	, "Nanite."
	, "https://bkaradzic.github.io/bgfx/examples.html#nanite" 
	);
