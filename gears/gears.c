//{{{  includes
#ifdef _WIN32
	#include <windows.h>
#endif

#include <GL/gl.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
	#include <X11/Xlib.h>
	#include <X11/keysym.h>

	#include <GL/glx.h>
	#include <GL/glxext.h>

	//#include <sys/time.h>
	//#include <unistd.h>
	#define GLX_MESA_swap_control 1
	typedef int (*PFNGLXGETSWAPINTERVALMESAPROC)(void);
#endif

#ifndef M_PI
	#define M_PI 3.14159265f
#endif
//}}}

#ifdef _WIN32
	//PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = 0;
	static HDC hDC;
	static HGLRC hRC;
	static HWND hWnd;
	static HINSTANCE hInst;
	static RECT winrect;
#endif

// common
static bool fullscreen = false;
static bool animate = true;
static GLint samples = 0;
static GLfloat view_rotx = 20.0, view_roty = 30.0, view_rotz = 0.0;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0;
//{{{
static int current_time() {
//return current time (in seconds)
	return (int)time(NULL);
	}
//}}}

//{{{
static void gear (GLfloat inner_radius, GLfloat outer_radius, GLfloat width, GLint teeth, GLfloat tooth_depth) {
//  Draw a gear wheel.  You'll probably want to call this function when
//  building a display list since we do a lot of trig here.
//  Input:  inner_radius - radius of hole at center
//          outer_radius - radius at center of teeth
//          width - width of gear
//          teeth - number of teeth
//          tooth_depth - depth of tooth

	GLint i;
	GLfloat r0, r1, r2;
	GLfloat angle, da;
	GLfloat u, v, len;

	r0 = inner_radius;
	r1 = outer_radius - tooth_depth / 2.0f;
	r2 = outer_radius + tooth_depth / 2.0f;

	da = 2.0f * M_PI / teeth / 4.0f;

	glShadeModel (GL_FLAT);

	glNormal3f (0.0, 0.0, 1.0);

	/* draw front face */
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i <= teeth; i++) {
		angle = i * 2.0f * M_PI / teeth;
		glVertex3f (r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
		glVertex3f (r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
		if (i < teeth) {
			glVertex3f (r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
			glVertex3f (r1 * cosf(angle + 3.f * da), r1 * sinf(angle + 3.f * da), width * 0.5f);
			}
		}
	glEnd();

	/* draw front sides of teeth */
	glBegin(GL_QUADS);
	da = 2.f * M_PI / teeth / 4.0f;
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0f * M_PI / teeth;
		glVertex3f (r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
		glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
		glVertex3f (r2 * cosf(angle + 2.0f * da), r2 * sinf(angle + 2.0f * da), width * 0.5f);
		glVertex3f (r1 * cosf(angle + 3.0f * da), r1 * sinf(angle + 3.0f * da), width * 0.5f);
		}
	glEnd();

	glNormal3f(0.0f, 0.0f, -1.0f);

	/* draw back face */
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i <= teeth; i++) {
		angle = i * 2.f * M_PI / teeth;
		glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
		glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
		if (i < teeth) {
			glVertex3f (r1 * cosf(angle + 3.0f * da), r1 * sinf(angle + 3.0f * da), -width * 0.5f);
			glVertex3f (r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
			}
		}
	glEnd();

	/* draw back sides of teeth */
	glBegin(GL_QUADS);
	da = 2.0f * M_PI / teeth / 4.0f;
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0f * M_PI / teeth;

		glVertex3f (r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
		glVertex3f (r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), -width * 0.5f);
		glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), -width * 0.5f);
		glVertex3f (r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
		}
	glEnd();

	/* draw outward faces of teeth */
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0f * M_PI / teeth;

		glVertex3f (r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
		glVertex3f (r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
		u = r2 * cosf(angle + da) - r1 * cosf(angle);
		v = r2 * sinf(angle + da) - r1 * sinf(angle);
		len = sqrtf(u * u + v * v);
		u /= len;
		v /= len;
		glNormal3f (v, -u, 0.0f);
		glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
		glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), -width * 0.5f);
		glNormal3f (cosf(angle), sinf(angle), 0.0f);
		glVertex3f (r2 * cosf(angle + 2.f * da), r2 * sinf(angle + 2.f * da), width * 0.5f);
		glVertex3f (r2 * cosf(angle + 2.f * da), r2 * sinf(angle + 2.f * da), -width * 0.5f);
		u = r1 * cosf(angle + 3.f * da) - r2 * cosf(angle + 2 * da);
		v = r1 * sinf(angle + 3.f * da) - r2 * sinf(angle + 2 * da);
		glNormal3f (v, -u, 0.0);
		glVertex3f (r1 * cosf(angle + 3.f * da), r1 * sinf(angle + 3.f * da), width * 0.5f);
		glVertex3f (r1 * cosf(angle + 3.f * da), r1 * sinf(angle + 3.f * da), -width * 0.5f);
		glNormal3f (cosf(angle), sinf(angle), 0.0);
	 }

	// VS2012 could not use cos & sin with integers, have to cast to double
	glVertex3f(r1 * cosf(0), r1 * sinf(0), width * 0.5f);
	glVertex3f(r1 * cosf(0), r1 * sinf(0), -width * 0.5f);

	glEnd();

	glShadeModel (GL_SMOOTH);

	/* draw inside radius cylinder */
	glBegin (GL_QUAD_STRIP);
	for (i = 0; i <= teeth; i++) {
		angle = i * 2.0f * M_PI / teeth;
		glNormal3f (-cosf(angle), -sinf(angle), 0.0f);
		glVertex3f (r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
		glVertex3f (r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
		}
	glEnd();
	}
//}}}
//{{{
static void init() {

	static GLfloat pos[4] = { 5.0f, 5.0f, 10.0f, 0.0f };
	static GLfloat red[4] = { 0.8f, 0.1f, 0.0f, 1.0f };
	static GLfloat green[4] = { 0.0f, 0.8f, 0.2f, 1.0f };
	static GLfloat blue[4] = { 0.2f, 0.2f, 1.0f, 1.0f };

	glLightfv (GL_LIGHT0, GL_POSITION, pos);
	glEnable (GL_CULL_FACE);
	glEnable (GL_LIGHTING);
	glEnable (GL_LIGHT0);
	glEnable (GL_DEPTH_TEST);

	// make the gears
	gear1 = glGenLists (1);
	glNewList (gear1, GL_COMPILE);
	glMaterialfv (GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
	gear (1.0f, 4.0f, 1.0f, 20, 0.7f);
	glEndList();

	gear2 = glGenLists (1);
	glNewList (gear2, GL_COMPILE);
	glMaterialfv (GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
	gear (0.5f, 2.0f, 2.0f, 10, 0.7f);
	glEndList();

	gear3 = glGenLists (1);
	glNewList (gear3, GL_COMPILE);
	glMaterialfv (GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
	gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
	glEndList();

	glEnable (GL_NORMALIZE);
	}
//}}}
//{{{
/* new window size or exposure */
static void reshape (int width, int height) {

	GLfloat h = (GLfloat) height / (GLfloat) width;

	glViewport (0, 0, (GLint) width, (GLint) height);
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity();
	glFrustum (-1.0, 1.0, -h, h, 5.0, 60.0);
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef (0.0, 0.0, -40.0);
	}
//}}}
//{{{
static void draw() {

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glPushMatrix();
	glRotatef (view_rotx, 1.0, 0.0, 0.0);
	glRotatef (view_roty, 0.0, 1.0, 0.0);
	glRotatef (view_rotz, 0.0, 0.0, 1.0);

	glPushMatrix();
	glTranslatef (-3.0, -2.0, 0.0);
	glRotatef (angle, 0.0, 0.0, 1.0);
	glCallList (gear1);
	glPopMatrix();

	glPushMatrix();
	glTranslatef (3.1f, -2.0f, 0.0f);
	glRotatef (-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
	glCallList (gear2);
	glPopMatrix();

	glPushMatrix();
	glTranslatef (-3.1f, 4.2f, 0.0f);
	glRotatef (-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
	glCallList (gear3);
	glPopMatrix();

	glPopMatrix();
	}
//}}}

#ifdef _WIN32
	//{{{
	static void event_loop() {

		int t, t0 = current_time();
		int frames = 0;

		MSG msg;
		while (1) {
			if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT) break;;
				TranslateMessage (&msg);
				DispatchMessage (&msg);
				}

			angle += 2.0;
			draw();
			SwapBuffers (hDC);

			// calc framerate
			t = current_time();
			frames++;
			if (t - t0 >= 5.0f) {
				GLfloat s = t - t0;
				GLfloat fps = frames / s;
				printf ("%d frames in %3.1f seconds = %6.3f FPS\n", frames, s, fps);
				t0 = t;
				frames = 0;
				}
			}
		}
	//}}}
	//{{{
	LRESULT CALLBACK WndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

		switch (uMsg) {
			case WM_CLOSE:
				PostQuitMessage(0);
				return 0;

			case WM_SIZE:
				reshape (LOWORD(lParam), HIWORD(lParam));
				return 0;

			case WM_KEYDOWN:
				if (wParam == VK_LEFT)
					view_roty += 5.0;
				else if (wParam == VK_RIGHT)
					view_roty -= 5.0;
				else if (wParam == VK_UP)
					view_rotx += 5.0;
				else if (wParam == VK_DOWN)
					view_rotx -= 5.0;
				else if (wParam == VK_ESCAPE)
					PostQuitMessage(0);
				return 0;
				}

		return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}
	//}}}
	//{{{
	static void make_window (const char* name, int x, int y, int width, int height) {
	// Create an RGB, double-buffered window,  Return the window and context handles.

		static PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR),
																				 1,
																				 PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
																				 PFD_TYPE_RGBA,
																				 24,
																				 0, 0, 0, 0, 0, 0,
																				 0,
																				 0,
																				 0,
																				 0, 0, 0, 0,
																				 16,
																				 0,
																				 0,
																				 PFD_MAIN_PLANE,
																				 0,
																				 0, 0, 0
																				 };

		winrect.left = (long)0;
		winrect.right = (long)width;
		winrect.top = (long) 0;
		winrect.bottom = (long)height;

		hInst = GetModuleHandle(NULL);
		WNDCLASS wc;
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = (WNDPROC)WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hInst;
		wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL;
		wc.lpszMenuName = NULL;
		wc.lpszClassName = name;
		if (!RegisterClass(&wc)) {
			printf ("failed to register class\n");
			exit (0);
			}

		DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
		DWORD dwStyle = WS_OVERLAPPEDWINDOW;
		AdjustWindowRectEx (&winrect, dwStyle, false, dwExStyle);

		if (!(hWnd = CreateWindowEx (dwExStyle, name, name,
																 WS_CLIPSIBLINGS | WS_CLIPCHILDREN | dwStyle, 0, 0,
																 winrect.right - winrect.left, winrect.bottom - winrect.top,
																 NULL, NULL, hInst, NULL))) {
			printf ("failed to create window\n");
			exit (0);
			}

		GLuint PixelFormat;
		if (!(hDC = GetDC (hWnd)) ||
				!(PixelFormat = ChoosePixelFormat (hDC, &pfd)) ||
				!SetPixelFormat (hDC, PixelFormat, &pfd) ||
				!(hRC = wglCreateContext (hDC)) ||
				!wglMakeCurrent (hDC, hRC)) {
			printf ("failed to initialise opengl\n");
			exit (0);
			}

		ShowWindow (hWnd, SW_SHOW);
		SetForegroundWindow (hWnd);
		SetFocus (hWnd);
		}
	//}}}
	//{{{
	int main (int argc, char *argv[]) {

		make_window ("gears", 0, 0, 300, 300);
		reshape (300, 300);

		// force vsync off
		#if 0
			wglSwapIntervalEXT = wglGetProcAddress ("wglSwapIntervalEXT");
			if (!wglSwapIntervalEXT)
				printf ("warning: wglSwapIntervalEXT missing, cannot force vsync off\n");
			else if (!wglSwapIntervalEXT (0))
				printf ("warning: failed to force vsync off, it may still be on\n");
		#endif

		printf ("GL_EXTENSIONS = %s\n", (char*) glGetString(GL_EXTENSIONS));
		printf ("GL_RENDERER   = %s\n", (char*) glGetString(GL_RENDERER));
		printf ("GL_VERSION    = %s\n", (char*) glGetString(GL_VERSION));
		printf ("GL_VENDOR     = %s\n", (char*) glGetString(GL_VENDOR));

		init();
		event_loop();

		wglMakeCurrent (NULL, NULL);
		wglDeleteContext (hRC);
		ReleaseDC (hWnd, hDC);

		return EXIT_SUCCESS;
		}
	//}}}
#else
	//{{{
	/** Draw single frame, do SwapBuffers, compute FPS */
	static void draw_frame (Display* dpy, Window win) {

		static int frames = 0;
		static double tRot0 = -1.0, tRate0 = -1.0;
		double dt, t = current_time();

		if (tRot0 < 0.0)
			tRot0 = t;
		dt = t - tRot0;
		tRot0 = t;

		if (animate) {
			/* advance rotation for next frame */
			angle += 70.0 * dt;  /* 70 degrees per second */
			if (angle > 3600.0)
				angle -= 3600.0;
			}

		draw();
		glXSwapBuffers (dpy, win);

		frames++;

		if (tRate0 < 0.0)
			tRate0 = t;
		if (t - tRate0 >= 5.0) {
			GLfloat seconds = t - tRate0;
			GLfloat fps = frames / seconds;
			printf ("%d frames in %3.1f seconds = %6.3f FPS\n", frames, seconds, fps);
			fflush (stdout);
			tRate0 = t;
			frames = 0;
			}
		}
	//}}}
	//{{{
	static int handle_event (Display* dpy, Window win, XEvent* event) {
	// Handle one X event, return NOP, EXIT or DRAW
		#define NOP 0
		#define EXIT 1
		#define DRAW 2

		switch (event->type) {
			case Expose:
				return DRAW;

			case ConfigureNotify:
				reshape (event->xconfigure.width, event->xconfigure.height);
				break;

			case KeyPress: {
				char buffer[10];
				int code = XLookupKeysym (&event->xkey, 0);
				if (code == XK_Left)
					view_roty += 5.0;
				else if (code == XK_Right)
					view_roty -= 5.0;
				else if (code == XK_Up)
					view_rotx += 5.0;
				else if (code == XK_Down)
					view_rotx -= 5.0;
				else {
					XLookupString (&event->xkey, buffer, sizeof(buffer), NULL, NULL);
					if (buffer[0] == 27) // escape
						return EXIT;
					else if (buffer[0] == 'a' || buffer[0] == 'A')
						animate = !animate;
					}
				return DRAW;
				}
			}

		return NOP;
		}
	//}}}
	//{{{
	static void event_loop (Display* dpy, Window win) {

		while (true) {
			while (!animate || XPending(dpy) > 0) {
				XEvent event;
				XNextEvent (dpy, &event);
				int op = handle_event (dpy, win, &event);
				if (op == EXIT)
					return;
				else if (op == DRAW)
					break;
				}

			draw_frame (dpy, win);
			}
		}
	//}}}

	//{{{
	static void no_border (Display* dpy, Window w) {
	// Remove window border/decorations.

		static const unsigned MWM_HINTS_DECORATIONS = (1 << 1);
		static const int PROP_MOTIF_WM_HINTS_ELEMENTS = 5;

		typedef struct {
			unsigned long flags;
			unsigned long functions;
			unsigned long decorations;
			long          inputMode;
			unsigned long status;
			} PropMotifWmHints;

		PropMotifWmHints motif_hints;
		Atom prop, proptype;
		unsigned long flags = 0;

		// setup the property
		motif_hints.flags = MWM_HINTS_DECORATIONS;
		motif_hints.decorations = flags;

		// get the atom for the property
		prop = XInternAtom (dpy, "_MOTIF_WM_HINTS", True );
		if (!prop)
			return;

		// not sure this is correct, seems to work, XA_WM_HINTS didn't work */
		proptype = prop;
		XChangeProperty (dpy, w,                        /* display, window */
										 prop, proptype,                /* property, type */
										 32,                            /* format: 32-bit datums */
										 PropModeReplace,               /* mode */
										 (unsigned char*) &motif_hints, /* data */
										 PROP_MOTIF_WM_HINTS_ELEMENTS   /* nelements */
										 );
		}
	//}}}
	//{{{
	static void make_window (Display* dpy, const char* name, int x, int y, int width, int height,
													 Window* winRet, GLXContext* ctxRet, VisualID* visRet) {
	// Create an RGB, double-buffered window, Return the window and context handles.

		int attribs[64];
		int i = 0;

		int scrnum;
		XSetWindowAttributes attr;
		unsigned long mask;
		Window root;
		Window win;
		GLXContext ctx;
		XVisualInfo *visinfo;

		// Singleton attributes
		attribs[i++] = GLX_RGBA;
		attribs[i++] = GLX_DOUBLEBUFFER;

		// Key/value attributes
		attribs[i++] = GLX_RED_SIZE;
		attribs[i++] = 1;
		attribs[i++] = GLX_GREEN_SIZE;
		attribs[i++] = 1;
		attribs[i++] = GLX_BLUE_SIZE;
		attribs[i++] = 1;
		attribs[i++] = GLX_DEPTH_SIZE;
		attribs[i++] = 1;
		if (samples > 0) {
			attribs[i++] = GLX_SAMPLE_BUFFERS;
			attribs[i++] = 1;
			attribs[i++] = GLX_SAMPLES;
			attribs[i++] = samples;
		 }
		attribs[i++] = None;

		scrnum = DefaultScreen (dpy);
		root = RootWindow (dpy, scrnum);

		visinfo = glXChooseVisual (dpy, scrnum, attribs);
		if (!visinfo) {
			printf ("Error: couldn't get an RGB, Double-buffered");
			if (samples > 0)
				printf(", Multisample");
			printf (" visual\n");
			exit (1);
			}

		// window attributes
		attr.background_pixel = 0;
		attr.border_pixel = 0;
		attr.colormap = XCreateColormap (dpy, root, visinfo->visual, AllocNone);
		attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
		// XXX this is a bad way to get a borderless window!
		mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
		win = XCreateWindow (dpy, root, x, y, width, height,
												 0, visinfo->depth, InputOutput, visinfo->visual, mask, &attr);
		if (fullscreen)
			no_border (dpy, win);

		// set hints and properties
			{
			XSizeHints sizehints;
			sizehints.x = x;
			sizehints.y = y;
			sizehints.width  = width;
			sizehints.height = height;
			sizehints.flags = USSize | USPosition;
			XSetNormalHints (dpy, win, &sizehints);
			XSetStandardProperties (dpy, win, name, name, None, (char**)NULL, 0, &sizehints);
			}

		ctx = glXCreateContext (dpy, visinfo, NULL, True );
		if (!ctx) {
			printf ("Error: glXCreateContext failed\n");
			exit (1);
			}

		*winRet = win;
		*ctxRet = ctx;
		*visRet = visinfo->visualid;

		XFree (visinfo);
		}
	//}}}
	//{{{
	static int is_glx_extension_supported (Display* dpy, const char* query) {
	// Determine whether or not a GLX extension is supported.

		const int scrnum = DefaultScreen(dpy);
		const char *glx_extensions = NULL;
		const size_t len = strlen(query);
		const char *ptr;

		if (glx_extensions == NULL)
			glx_extensions = glXQueryExtensionsString (dpy, scrnum);

		ptr = strstr(glx_extensions, query);
		return ((ptr != NULL) && ((ptr[len] == ' ') || (ptr[len] == '\0')));
		}
	//}}}
	//{{{
	static void query_vsync (Display* dpy, GLXDrawable drawable) {
	// Attempt to determine whether or not the display is synched to vblank.

		int interval = 0;

		#if defined(GLX_EXT_swap_control)
			if (is_glx_extension_supported(dpy, "GLX_EXT_swap_control")) {
				unsigned int tmp = -1;
				glXQueryDrawable(dpy, drawable, GLX_SWAP_INTERVAL_EXT, &tmp);
				interval = tmp;
				}
			else
		#endif

		if (is_glx_extension_supported(dpy, "GLX_MESA_swap_control")) {
			PFNGLXGETSWAPINTERVALMESAPROC pglXGetSwapIntervalMESA =
					(PFNGLXGETSWAPINTERVALMESAPROC)glXGetProcAddressARB((const GLubyte *) "glXGetSwapIntervalMESA");
			interval = (*pglXGetSwapIntervalMESA)();
			}

		else if (is_glx_extension_supported(dpy, "GLX_SGI_swap_control")) {
			/* The default swap interval with this extension is 1.  Assume that it
			 * is set to the default.
			 * Many Mesa-based drivers default to 0, but all of these drivers also
			 * export GLX_MESA_swap_control.  In that case, this branch will never
			 * be taken, and the correct result should be reported.
			 */
			interval = 1;
			}

		if (interval > 0) {
			printf ("Running synchronized to the vertical refresh.  The framerate should be\n");
			if (interval == 1)
				printf ("approximately the same as the monitor refresh rate.\n");
			else if (interval > 1)
				printf ("approximately 1/%d the monitor refresh rate.\n", interval);
			}
		}
	//}}}
	//{{{
	int main (int argc, char* argv[]) {

		char* dpyName = NULL;

		int x = 0;
		int y = 0;
		unsigned int winWidth = 300;
		unsigned int winHeight = 300;

		for (int i = 1; i < argc; i++)
			if (strcmp(argv[i], "-f") == 0)
				fullscreen = GL_TRUE;
			else if (strcmp(argv[i], "-d") == 0) {
				dpyName = argv[i+1];
				i++;
				}
			else if (i < argc-1 && strcmp(argv[i], "-s") == 0) {
				samples = strtod (argv[i+1], NULL );
				++i;
				}
			else if (i < argc-1 && strcmp(argv[i], "-g") == 0) {
				XParseGeometry (argv[i+1], &x, &y, &winWidth, &winHeight);
				i++;
				}

		Display* dpy = XOpenDisplay (dpyName);
		if (!dpy) {
			printf("Error: couldn't open display %s\n",
			dpyName ? dpyName : getenv("DISPLAY"));
			return -1;
			}

		if (fullscreen) {
			int scrnum = DefaultScreen (dpy);
			x = 0;
			y = 0;
			winWidth = DisplayWidth (dpy, scrnum);
			winHeight = DisplayHeight (dpy, scrnum);
			}

		Window win;
		GLXContext ctx;
		VisualID visId;
		make_window (dpy, "gears", x, y, winWidth, winHeight, &win, &ctx, &visId);

		XMapWindow (dpy, win);
		glXMakeCurrent (dpy, win, ctx);
		query_vsync (dpy, win);

		printf ("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
		printf ("VisualID %d, 0x%x\n", (int) visId, (int) visId);
		printf ("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
		printf ("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
		printf ("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));

		init();
		reshape (winWidth, winHeight);
		event_loop (dpy, win);

		glDeleteLists (gear1, 1);
		glDeleteLists (gear2, 1);
		glDeleteLists (gear3, 1);
		glXMakeCurrent (dpy, None, NULL);
		glXDestroyContext (dpy, ctx);
		XDestroyWindow (dpy, win);
		XCloseDisplay (dpy);

		return 0;
		}
	//}}}
#endif
