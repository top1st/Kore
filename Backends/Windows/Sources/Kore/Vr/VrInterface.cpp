#include "pch.h"

#include "VrInterface.h"

#ifdef VR_RIFT

#include "Kore/Log.h"

#include "GL/CAPI_GLE.h"
#include "Extras/OVR_Math.h"
#include "OVR_CAPI_GL.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <assert.h>

namespace Kore {

	ovrSession session;
	ovrHmdDesc hmdDesc;
	long long frameIndex;

	//-------------------------------------------------------------------------------------------
	struct OGL
	{
		HWND                    Window;
		HDC                     hDC;
		HGLRC                   WglContext;
		OVR::GLEContext         GLEContext;
		bool                    Running;
		bool                    Key[256];
		int                     WinSizeW;
		int                     WinSizeH;
		GLuint                  fboId;
		HINSTANCE               hInstance;

		static LRESULT CALLBACK WindowProc(_In_ HWND hWnd, _In_ UINT Msg, _In_ WPARAM wParam, _In_ LPARAM lParam)
		{
			OGL *p = reinterpret_cast<OGL *>(GetWindowLongPtr(hWnd, 0));
			switch (Msg)
			{
			case WM_KEYDOWN:
				p->Key[wParam] = true;
				break;
			case WM_KEYUP:
				p->Key[wParam] = false;
				break;
			case WM_DESTROY:
				p->Running = false;
				break;
			default:
				return DefWindowProcW(hWnd, Msg, wParam, lParam);
			}
			if ((p->Key['Q'] && p->Key[VK_CONTROL]) || p->Key[VK_ESCAPE])
			{
				p->Running = false;
			}
			return 0;
		}

		OGL() :
			Window(nullptr),
			hDC(nullptr),
			WglContext(nullptr),
			GLEContext(),
			Running(false),
			WinSizeW(0),
			WinSizeH(0),
			fboId(0),
			hInstance(nullptr)
		{
			// Clear input
			for (int i = 0; i < sizeof(Key) / sizeof(Key[0]); ++i)
				Key[i] = false;
		}

		~OGL()
		{
			ReleaseDevice();
			CloseWindow();
		}

		bool InitWindowAndDevice(HINSTANCE hInst, LPCWSTR title)
		{
			// Init window
			hInstance = hInst;
			Running = true;

			WNDCLASSW wc;
			memset(&wc, 0, sizeof(wc));
			wc.style = CS_CLASSDC;
			wc.lpfnWndProc = WindowProc;
			wc.cbWndExtra = sizeof(struct OGL *);
			wc.hInstance = GetModuleHandleW(NULL);
			wc.lpszClassName = L"ORT";
			RegisterClassW(&wc);

			// adjust the window size and show at InitDevice time
			Window = CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0, hInstance, 0);
			//Window = CreateWindowA("ORT", "ORT(OpenGL)", WS_POPUP, 0, 0, 1000, 1000, GetDesktopWindow(), NULL, hInstance, NULL);
			if (!Window) {
				log(Info, "Failed to open window.");
				return false;
			}

			SetWindowLongPtr(Window, 0, LONG_PTR(this));

			hDC = GetDC(Window);

			// Init device
			ovrGraphicsLuid luid;
			ovrResult result = ovr_Create(&session, &luid);
			if (!OVR_SUCCESS(result)) {
				log(Info, "HMD not connected.");
				return false; // todo: retry
			}

			hmdDesc = ovr_GetHmdDesc(session);

			// Setup Window and Graphics
			// Note: the mirror window can be any size, for this sample we use 1/2 the HMD resolution
			ovrSizei windowSize = { hmdDesc.Resolution.w / 2, hmdDesc.Resolution.h / 2 };
			if (!InitDevice(windowSize.w, windowSize.h, reinterpret_cast<LUID*>(&luid))) {
				ReleaseDevice();
				ovr_Destroy(session);
			}

			// FloorLevel will give tracking poses where the floor height is 0
			ovr_SetTrackingOriginType(session, ovrTrackingOrigin_FloorLevel);

			return true;
		}

		void CloseWindow()
		{
			if (Window)
			{
				if (hDC)
				{
					ReleaseDC(Window, hDC);
					hDC = nullptr;
				}
				DestroyWindow(Window);
				Window = nullptr;
				UnregisterClassW(L"OGL", hInstance);
			}
		}

		// Note: currently there is no way to get GL to use the passed pLuid
		bool InitDevice(int vpW, int vpH, const LUID*, bool windowed = true)
		{
			UNREFERENCED_PARAMETER(windowed);

			WinSizeW = vpW;
			WinSizeH = vpH;

			RECT size = { 0, 0, vpW, vpH };
			AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, false);
			const UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW;
			if (!SetWindowPos(Window, nullptr, 0, 0, size.right - size.left, size.bottom - size.top, flags))
				return false;

			PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARBFunc = nullptr;
			PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARBFunc = nullptr;
			{
				// First create a context for the purpose of getting access to wglChoosePixelFormatARB / wglCreateContextAttribsARB.
				PIXELFORMATDESCRIPTOR pfd;
				memset(&pfd, 0, sizeof(pfd));
				pfd.nSize = sizeof(pfd);
				pfd.nVersion = 1;
				pfd.iPixelType = PFD_TYPE_RGBA;
				pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
				pfd.cColorBits = 32;
				pfd.cDepthBits = 16;
				int pf = ChoosePixelFormat(hDC, &pfd);
				if (!pf) {
					log(Warning, "Failed to choose pixel format.");
					ReleaseDC(Window, hDC);
					return false;
				}

				if (!SetPixelFormat(hDC, pf, &pfd)) {
					log(Warning, "Failed to set pixel format.");
					ReleaseDC(Window, hDC);
					return false;
				}

				HGLRC context = wglCreateContext(hDC);
				if (!context) {
					log(Warning, "wglCreateContextfailed.");
					ReleaseDC(Window, hDC);
					return false;
				}
				if (!wglMakeCurrent(hDC, context)) {
					log(Warning, "wglMakeCurrent failed.");
					wglDeleteContext(context);
					ReleaseDC(Window, hDC);
					return false;
				}

				wglChoosePixelFormatARBFunc = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
				wglCreateContextAttribsARBFunc = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
				assert(wglChoosePixelFormatARBFunc && wglCreateContextAttribsARBFunc); //OVR_ASSERT

				wglDeleteContext(context);
			}

			// Now create the real context that we will be using.
			int iAttributes[] =
			{
				// WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
				WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
				WGL_COLOR_BITS_ARB, 32,
				WGL_DEPTH_BITS_ARB, 16,
				WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
				WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, GL_TRUE,
				0, 0
			};

			float fAttributes[] = { 0, 0 };
			int   pf = 0;
			UINT  numFormats = 0;

			if (!wglChoosePixelFormatARBFunc(hDC, iAttributes, fAttributes, 1, &pf, &numFormats)) {
				log(Warning, "wglChoosePixelFormatARBFunc failed.");
				ReleaseDC(Window, hDC);
				return false;
			}

			PIXELFORMATDESCRIPTOR pfd;
			memset(&pfd, 0, sizeof(pfd));
			if (!SetPixelFormat(hDC, pf, &pfd)) {
				log(Warning, "SetPixelFormat failed.");
				ReleaseDC(Window, hDC);
				return false;
			}

			GLint attribs[16];
			int   attribCount = 0;
			attribs[attribCount] = 0;

			WglContext = wglCreateContextAttribsARBFunc(hDC, 0, attribs);
			if (!wglMakeCurrent(hDC, WglContext)) {
				log(Warning, "wglMakeCurrent failed.");
				wglDeleteContext(WglContext);
				ReleaseDC(Window, hDC);
				return false;
			}

			OVR::GLEContext::SetCurrentContext(&GLEContext);
			GLEContext.Init();

			//ShowWindow(Window, SW_SHOWDEFAULT); // TODO: ???

			glGenFramebuffers(1, &fboId);

			glEnable(GL_DEPTH_TEST);
			glFrontFace(GL_CW);
			glEnable(GL_CULL_FACE);

			//SetCapture(Platform.Window);	// TODO: ???
			//ShowCursor(FALSE);				// TODO: ???

			return true;
		}

		bool HandleMessages(void)
		{
			MSG msg;
			while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			return Running;
		}

		void ReleaseDevice()
		{
			if (fboId)
			{
				glDeleteFramebuffers(1, &fboId);
				fboId = 0;
			}
			if (WglContext)
			{
				wglMakeCurrent(NULL, NULL);
				wglDeleteContext(WglContext);
				WglContext = nullptr;
			}
			GLEContext.Shutdown();
		}

	};

	// Global OpenGL state
	static OGL Platform;

	//------------------------------------------------------------------------------

	namespace VrInterface {

		void* Init(void* hinst) {
			ovrInitParams initParams = { ovrInit_RequestVersion, OVR_MINOR_VERSION, NULL, 0, 0 };
			ovrResult result = ovr_Initialize(&initParams);
			if (!OVR_SUCCESS(result)) {
				log(Warning, "Failed to initialize libOVR.");
				return(0);
			}

			if (!Platform.InitWindowAndDevice((HINSTANCE)hinst, L"ORT(OpenGL)")) {
				log(Warning, "Failed to init window and device.");
				return(0);
			}

			return Platform.Window;
		}

		void ovrShutdown() {
			ovr_Shutdown();
		}

		SensorState* getSensorState() {

			SensorState* sensorState = new SensorState();

			// Call ovr_GetRenderDesc each frame to get the ovrEyeRenderDesc, as the returned values (e.g. HmdToEyeOffset) may change at runtime.
			ovrEyeRenderDesc eyeRenderDesc[2];
			eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
			eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);

			// Get eye poses, feeding in correct IPD offset
			ovrPosef EyeRenderPose[2];
			ovrVector3f HmdToEyeOffset[2] = { eyeRenderDesc[0].HmdToEyeOffset, eyeRenderDesc[1].HmdToEyeOffset };
			
			// Get predicted eye pose
			double sensorSampleTime;    // sensorSampleTime is fed into the layer later
			ovr_GetEyePoses(session, frameIndex, ovrTrue, HmdToEyeOffset, EyeRenderPose, &sensorSampleTime);
			frameIndex++;

			ovrSessionStatus sessionStatus;
			ovr_GetSessionStatus(session, &sessionStatus);

			VrPoseState* poseState[2];
			poseState[0] = new VrPoseState();
			poseState[1] = new VrPoseState();

			for (int eye = 0; eye < 2; ++eye) {
				ovrQuatf orientation = EyeRenderPose[eye].Orientation;
				poseState[eye]->vrPose->orientation = Quaternion(orientation.x, orientation.y, orientation.z, orientation.w);

				ovrVector3f pos = EyeRenderPose[eye].Position;
				poseState[eye]->vrPose->position = vec3(pos.x, pos.y, pos.z);

				ovrFovPort fov = hmdDesc.DefaultEyeFov[eye];
				poseState[eye]->vrPose->left = fov.LeftTan;
				poseState[eye]->vrPose->right = fov.RightTan;
				poseState[eye]->vrPose->bottom = fov.DownTan;
				poseState[eye]->vrPose->top = fov.UpTan;
			}

			// TODO: get angular and linear sensor data
			//poseState->AngularAcceleration = 
			sensorState->predicted = poseState[0];
			sensorState->recorded = sensorState->predicted; // TODO: ist this necessary

			if (sessionStatus.IsVisible) sensorState->isVisible = true;
			else sensorState->isVisible = false;
			if (sessionStatus.HmdPresent) sensorState->hmdPresenting = true;
			else sensorState->hmdPresenting = false;
			if (sessionStatus.HmdMounted) sensorState->hmdMounted = true;
			else sensorState->hmdMounted = false;
			if (sessionStatus.DisplayLost) sensorState->displayLost = true;
			else sensorState->displayLost = false;
			if (sessionStatus.ShouldQuit) sensorState->shouldQuit = true;
			else sensorState->shouldQuit = false;
			if (sessionStatus.ShouldRecenter) sensorState->shouldRecenter = true;
			else sensorState->shouldRecenter = false;

			return sensorState;
		}

		void changeTrackingOrigin(bool standUp) {
			if (standUp) {
				ovr_SetTrackingOriginType(session, ovrTrackingOrigin_EyeLevel);
			} else {
				// FloorLevel will give tracking poses where the floor height is 0
				ovr_SetTrackingOriginType(session, ovrTrackingOrigin_FloorLevel);
			}
		}

		void recenterTracking() {
			ovr_RecenterTrackingOrigin(session);
		}

		void getHMDRessolution(int eye, int& w, int&h) {
			ovrSizei idealTextureSize = ovr_GetFovTextureSize(session, ovrEyeType(eye), hmdDesc.DefaultEyeFov[eye], 1);
			w = idealTextureSize.w;
			h = idealTextureSize.h;
		}
	}
}
#endif