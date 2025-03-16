/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "common.h"
#include "bgfx/bgfx.h"
#include "bgfx_utils.h"
#include "imgui/imgui.h"

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


class ExampleNanite : public entry::AppI
{
public:
	ExampleNanite(const char* _name, const char* _description, const char* _url)
		: entry::AppI(_name, _description, _url)
	{
	}

	void init(int32_t _argc, const char* const* _argv, uint32_t _width, uint32_t _height) override
	{
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



		// Enable debug text.
		bgfx::setDebug(m_debug);

		// Set view 0 clear state.
		bgfx::setViewClear(0
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x303030ff
			, 1.0f
			, 0
		);


		// Set geometry pass view clear state.
		bgfx::setViewClear(kRenderPassBaseGeometry
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 1.0f
			, 0
			, 1
			, 0
		);

		// Set light pass view clear state.
		bgfx::setViewClear(kRenderPassNaniteGeometry
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 1.0f
			, 0
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
		m_baseRT = bgfx::createTexture2D(
			bgfx::BackbufferRatio::Equal, false, 1, bgfx::TextureFormat::RGBA8, bilinearFlags);
		m_naniteRT = bgfx::createTexture2D(
			bgfx::BackbufferRatio::Equal, false, 1, bgfx::TextureFormat::RGBA8, bilinearFlags);


		m_samplerBase = bgfx::createUniform("samplerBase", bgfx::UniformType::Sampler);
		m_samplerNanite = bgfx::createUniform("samplerNanite", bgfx::UniformType::Sampler);

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

	void renderBase()
	{
		bgfx::setViewFrameBuffer(kRenderPassBaseGeometry, BGFX_INVALID_HANDLE);

		bgfx::setState(BGFX_STATE_MSAA, 0);

		// Set view 0 default viewport.
		bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height));

		// This dummy draw call is here to make sure that view 0 is cleared
		// if no other draw calls are submitted to view 0.
		bgfx::touch(0);

		float time = (float)((bx::getHPCounter() - m_timeOffset) / double(bx::getHPFrequency()));
		bgfx::setUniform(u_time, &time);

		const bx::Vec3 at = { 0.0f, 1.0f,  0.0f };
		const bx::Vec3 eye = { 0.0f, 1.0f, -2.5f };

		// Set view and projection matrix for view 0.
		{
			float view[16];
			bx::mtxLookAt(view, eye, at);

			float proj[16];
			bx::mtxProj(proj, 60.0f, float(m_width) / float(m_height), 0.1f, 100.0f, bgfx::getCaps()->homogeneousDepth);
			bgfx::setViewTransform(kRenderPassBaseGeometry, view, proj);

			// Set view 0 default viewport.
			bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height));
		}

		float mtx[16];
		bx::mtxRotateXY(mtx
			, 0.0f
			, 0.0f
		);

		meshSubmit(m_mesh, 0, m_program, mtx);
	}

	void renderNanite()
	{
		bgfx::setViewFrameBuffer(kRenderPassNaniteGeometry, BGFX_INVALID_HANDLE);


		bgfx::setState(BGFX_STATE_MSAA);

		// Set view 0 default viewport.
		bgfx::setViewRect(kRenderPassNaniteGeometry, 0, 0, uint16_t(m_width), uint16_t(m_height));

		// This dummy draw call is here to make sure that view 0 is cleared
		// if no other draw calls are submitted to view 0.
		bgfx::touch(0);

		float time = (float)((bx::getHPCounter() - m_timeOffset) / double(bx::getHPFrequency()));
		bgfx::setUniform(u_time, &time);

		const bx::Vec3 at = { 0.0f, 1.0f,  0.0f };
		const bx::Vec3 eye = { 0.0f, 1.0f, -2.5f };

		// Set view and projection matrix for view 0.
		{
			float view[16];
			bx::mtxLookAt(view, eye, at);

			float proj[16];
			bx::mtxProj(proj, 60.0f, float(m_width) / float(m_height), 0.1f, 100.0f, bgfx::getCaps()->homogeneousDepth);
			bgfx::setViewTransform(kRenderPassNaniteGeometry, view, proj);

			// Set view 0 default viewport.
			bgfx::setViewRect(kRenderPassNaniteGeometry, 0, 0, uint16_t(m_width), uint16_t(m_height));
		}

		float mtx[16];
		bx::mtxRotateXY(mtx
			, 0.0f
			, 0.0f
		);

		meshSubmit(m_mesh, 0, m_program, mtx);
	}

	bool update() override
	{
		if (!entry::processEvents(m_width, m_height, m_debug, m_reset, &m_mouseState))
		{
			/*
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

			imguiEndFrame();

			*/

			renderBase();
			renderNanite();



			// Combine color and light buffers.
			bgfx::setTexture(0, m_samplerBase, m_baseRT);
			bgfx::setTexture(1, m_samplerNanite, m_naniteRT);
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
			);

			bgfx::setState(BGFX_STATE_MSAA, 0);


			screenSpaceQuad(true);

			// Advance to next frame. Rendering thread will be kicked to
			// process submitted rendering primitives.
			bgfx::frame();

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
	Mesh* m_mesh;
	bgfx::ProgramHandle m_program;
	bgfx::ProgramHandle m_displayProgram;
	bgfx::UniformHandle u_time;

	bgfx::TextureHandle m_baseRT;
	bgfx::TextureHandle m_naniteRT;


	bgfx::UniformHandle m_samplerBase;
	bgfx::UniformHandle m_samplerNanite;

	const bgfx::ViewId kRenderPassBaseGeometry = 1;
	const bgfx::ViewId kRenderPassNaniteGeometry = 2;


	enum RenderPassType
	{
		BasePass = 0,
		NanitePass = 1,
		CombinePass = 2
	};
};

} // namespace

ENTRY_IMPLEMENT_MAIN(
	ExampleNanite
	, "50-nanite"
	, "Nanite."
	, "https://bkaradzic.github.io/bgfx/examples.html#nanite" 
	);
