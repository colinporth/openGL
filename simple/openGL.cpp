// openGL.cpp : Defines the entry point for the console application.
//{{{  includes
#include <windows.h>

#include <stdio.h>
#include <tchar.h>
#include <math.h>

#include <GL/gl.h>
#include <GL/glu.h>

#pragma comment (lib,"OpenGL32.lib")
#pragma comment (lib,"glu32.lib")
//}}}

#define WIDTH           800
#define HEIGHT          600
#define M_PI 3.14159265f

#define BLACK_INDEX     0
#define RED_INDEX       13
#define GREEN_INDEX     14
#define BLUE_INDEX      16

#define GLOBE    1
#define CYLINDER 2
#define CONE     3

CHAR szAppName[] = "Windows OpenGL";
HWND hWnd;
HDC hDC;
HGLRC hGLRC;

GLfloat latitude, longitude, latinc, longinc;
GLdouble radius;

static GLfloat view_rotx = 20.0f, view_roty = 30.0f, view_rotz = 0.0f;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0f;
static GLfloat pos[4] = { 5.0f, 5.0f, 10.0f, 0.0f };
static GLfloat red[4] = { 0.8f, 0.1f, 0.0f, 1.0f };
static GLfloat green[4] = { 0.0f, 0.8f, 0.2f, 1.0f };
static GLfloat blue[4] = { 0.2f, 0.2f, 1.0f, 1.0f };

//{{{
bool setupPixelFormat (HDC hdc) {

  PIXELFORMATDESCRIPTOR pfd;
  PIXELFORMATDESCRIPTOR* ppfd = &pfd;
  ppfd->nSize = sizeof(PIXELFORMATDESCRIPTOR);
  ppfd->nVersion = 1;
  ppfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  ppfd->dwLayerMask = PFD_MAIN_PLANE;
  ppfd->iPixelType = PFD_TYPE_COLORINDEX;
  ppfd->cColorBits = 8;
  ppfd->cDepthBits = 16;
  ppfd->cAccumBits = 0;
  ppfd->cStencilBits = 0;

  int pixelformat = ChoosePixelFormat (hdc, ppfd);
  if (pixelformat == 0) {
    printf ("ChoosePixelFormat failed");
    return false;
    }
  if (SetPixelFormat (hdc, pixelformat, ppfd) == FALSE) {
    printf ("SetPixelFormat failed");
    return false;
    }

  return true;
  }
//}}}
//{{{
void polarView (GLdouble radius, GLdouble twist, GLdouble latitude, GLdouble longitude) {

  glTranslated (0.0, 0.0, -radius);
  glRotated (-twist, 0.0, 0.0, 1.0);
  glRotated (-latitude, 1.0, 0.0, 0.0);
  glRotated (longitude, 0.0, 0.0, 1.0);
  }
//}}}

//{{{
void createObjects() {

  glNewList (GLOBE, GL_COMPILE);
  GLUquadricObj* quadObj = gluNewQuadric();
  gluQuadricDrawStyle (quadObj, GLU_LINE);
  gluSphere (quadObj, 1.5, 16, 16);
  glEndList();

  glNewList (CONE, GL_COMPILE);
  quadObj = gluNewQuadric();
  gluQuadricDrawStyle (quadObj, GLU_FILL);
  gluQuadricNormals (quadObj, GLU_SMOOTH);
  gluCylinder (quadObj, 0.3, 0.0, 0.6, 15, 10);
  glEndList();

  glNewList (CYLINDER, GL_COMPILE);
  glPushMatrix();
  glRotatef ((GLfloat)90.0, (GLfloat)1.0, (GLfloat)0.0, (GLfloat)0.0);
  glTranslatef ((GLfloat)0.0, (GLfloat)0.0, (GLfloat)-1.0);
  quadObj = gluNewQuadric();
  gluQuadricDrawStyle (quadObj, GLU_FILL);
  gluQuadricNormals (quadObj, GLU_SMOOTH);
  gluCylinder (quadObj, 0.3, 0.3, 0.6, 12, 2);
  glPopMatrix();
  glEndList();
  }
//}}}
//{{{
void createGear (GLfloat inner_radius, GLfloat outer_radius, GLfloat width, GLint teeth, GLfloat tooth_depth) {

  GLfloat r0 = inner_radius;
  GLfloat r1 = outer_radius - tooth_depth / 2.0f;
  GLfloat r2 = outer_radius + tooth_depth / 2.0f;
  GLfloat da = 2.0f * M_PI / teeth / 4.0f;

  glShadeModel (GL_FLAT);

  glNormal3f (0.0, 0.0, 1.0);

  /* draw front face */
  glBegin (GL_QUAD_STRIP);
  for (GLint i = 0; i <= teeth; i++) {
    GLfloat angle = i * 2.0f * M_PI / teeth;
    glVertex3f (r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
    glVertex3f (r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
    if (i < teeth) {
      glVertex3f (r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
      glVertex3f (r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
      }
    }
  glEnd();

  /* draw front sides of teeth */
  glBegin (GL_QUADS);
  da = 2.0f * M_PI / teeth / 4.0f;
  for (GLint i = 0; i < teeth; i++) {
    GLfloat angle = i * 2.0f * M_PI / teeth;
    glVertex3f (r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
    glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
    glVertex3f (r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), width * 0.5f);
    glVertex3f (r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
    }
  glEnd();

  glNormal3f (0.0, 0.0, -1.0);

  /* draw back face */
  glBegin (GL_QUAD_STRIP);
  for (GLint i = 0; i <= teeth; i++) {
    angle = i * 2.0f * M_PI / teeth;
    glVertex3f (r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
    glVertex3f (r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
    if (i < teeth) {
      glVertex3f (r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
      glVertex3f (r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
      }
    }
  glEnd();

  /* draw back sides of teeth */
  glBegin (GL_QUADS);
  da = 2.0f * M_PI / teeth / 4.0f;
  for (GLint i = 0; i < teeth; i++) {
    GLfloat angle = i * 2.0f * M_PI / teeth;
    glVertex3f (r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
    glVertex3f (r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), -width * 0.5f);
    glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), -width * 0.5f);
    glVertex3f (r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
    }
  glEnd();

  /* draw outward faces of teeth */
  glBegin (GL_QUAD_STRIP);
  for (GLint i = 0; i < teeth; i++) {
    GLfloat angle = i * 2.0f * M_PI / teeth;

    glVertex3f (r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
    glVertex3f (r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
    GLfloat u = r2 * cosf(angle + da) - r1 * cosf(angle);
    GLfloat v = r2 * sinf(angle + da) - r1 * sinf(angle);
    GLfloat len = sqrtf (u * u + v * v);
    u /= len;
    v /= len;
    glNormal3f (v, -u, 0.0);
    glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
    glVertex3f (r2 * cosf(angle + da), r2 * sinf(angle + da), -width * 0.5f);
    glNormal3f (cosf(angle), sinf(angle), 0.0);
    glVertex3f (r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), width * 0.5f);
    glVertex3f (r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), -width * 0.5f);
    u = r1 * cosf(angle + 3 * da) - r2 * cosf(angle + 2 * da);
    v = r1 * sinf(angle + 3 * da) - r2 * sinf(angle + 2 * da);
    glNormal3f (v, -u, 0.0);
    glVertex3f (r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
    glVertex3f (r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
    glNormal3f (cosf(angle), sinf(angle), 0.0);
    }

  glVertex3f (r1 * cosf(0), r1 * sinf(0), width * 0.5f);
  glVertex3f (r1 * cosf(0), r1 * sinf(0), -width * 0.5f);
  glEnd();

  glShadeModel (GL_SMOOTH);

  /* draw inside radius cylinder */
  glBegin (GL_QUAD_STRIP);
  for (GLint i = 0; i <= teeth; i++) {
    GLfloat angle = i * 2.0f * M_PI / teeth;
    glNormal3f (-cosf(angle), -sinf(angle), 0.0f);
    glVertex3f (r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
    glVertex3f (r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
    }
  glEnd();
  }
//}}}
//{{{
void initGears() {

  glLightfv (GL_LIGHT0, GL_POSITION, pos);
  glEnable (GL_CULL_FACE);
  glEnable (GL_LIGHTING);
  glEnable (GL_LIGHT0);
  glEnable (GL_DEPTH_TEST);

  /* make the gears */
  gear1 = glGenLists (1);
  glNewList (gear1, GL_COMPILE);
  glMaterialfv (GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
  createGear (1.0f, 4.0f, 1.0f, 20, 0.7f);
  glEndList();

  gear2 = glGenLists (1);
  glNewList (gear2, GL_COMPILE);
  glMaterialfv (GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
  createGear (0.5f, 2.0f, 2.0f, 10, 0.7f);
  glEndList();

  gear3 = glGenLists (1);
  glNewList (gear3, GL_COMPILE);
  glMaterialfv (GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
  createGear (1.3f, 2.0f, 0.5f, 10, 0.7f);
  glEndList();

  glEnable (GL_NORMALIZE);
  }
//}}}

//{{{
void initialize (GLsizei width, GLsizei height) {

  glClearIndex ((GLfloat)BLACK_INDEX);
  glClearDepth (1.0);

  glEnable (GL_DEPTH_TEST);

  glMatrixMode (GL_PROJECTION);
  GLfloat aspect = (GLfloat) width / height;
  gluPerspective (45.0, aspect, 3.0, 7.0 );
  glMatrixMode (GL_MODELVIEW);

  GLdouble near_plane = 3.0;
  GLdouble far_plane = 7.0;
  GLfloat maxObjectSize = 3.0F;
  radius = near_plane + maxObjectSize/2.0;

  latitude = 0.0F;
  longitude = 0.0F;
  latinc = 6.0F;
  longinc = 2.5F;

  createObjects();
  initGears();
  }
//}}}
//{{{
void resize (GLsizei width, GLsizei height ) {

  glViewport (0, 0, width, height);
  GLfloat aspect = (GLfloat)width / height;

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity();
  gluPerspective (45.0, aspect, 3.0, 7.0);
  glMatrixMode (GL_MODELVIEW);
  }
//}}}
//{{{
void drawScene() {

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glPushMatrix();
  latitude += latinc;
  longitude += longinc;
  polarView (radius, 0, latitude, longitude);

  glIndexi (RED_INDEX);
  glCallList (CONE);

  glIndexi (BLUE_INDEX);
  glCallList (GLOBE);

  glIndexi (GREEN_INDEX);
  glPushMatrix();
  glTranslatef (0.8F, -0.65F, 0.0F);
  glRotatef (30.0F, 1.0F, 0.5F, 1.0F);
  glCallList (CYLINDER);
  glPopMatrix();

  glPopMatrix();

  // draw gears
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

  SwapBuffers (hDC);
  Sleep (20);
  }
//}}}

//{{{
void GLreport() {
  printf ("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
  printf ("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
  printf ("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));
  printf( "GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
  }
//}}}
//{{{
LONG WINAPI WndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

  LONG lRet = 1;
  PAINTSTRUCT ps;
  RECT rect;

  switch (uMsg) {
    //{{{
    case WM_CREATE:
      hDC = GetDC (hWnd);
      if (!setupPixelFormat (hDC))
        PostQuitMessage (0);

      hGLRC = wglCreateContext (hDC);
      wglMakeCurrent (hDC, hGLRC);
      GetClientRect (hWnd, &rect);

      initialize (rect.right, rect.bottom);
      break;
    //}}}
    //{{{
    case WM_PAINT:
      BeginPaint (hWnd, &ps);
      EndPaint (hWnd, &ps);
      break;
    //}}}
    //{{{
    case WM_SIZE:
      GetClientRect(hWnd, &rect);
      resize (rect.right, rect.bottom);
      break;
    //}}}
    //{{{
    case WM_CLOSE:
      if (hGLRC)
        wglDeleteContext (hGLRC);
      if (hDC)
        ReleaseDC (hWnd, hDC);
      hGLRC = 0;
      hDC = 0;
      DestroyWindow (hWnd);
      break;
    //}}}
    //{{{
    case WM_DESTROY:
      if (hGLRC)
        wglDeleteContext (hGLRC);
      if (hDC)
        ReleaseDC (hWnd, hDC);
      PostQuitMessage (0);
      break;
    //}}}
    //{{{
    case WM_KEYDOWN:
      switch (wParam) {
        case VK_LEFT:
          longinc += 0.5F;
          break;
        case VK_RIGHT:
          longinc -= 0.5F;
          break;
        case VK_UP:
          latinc += 0.5F;
          break;
        case VK_DOWN:
          latinc -= 0.5F;
          break;
        }
    //}}}
    default:
      lRet = (LONG)DefWindowProc (hWnd, uMsg, wParam, lParam);
      break;
    }

  return lRet;
  }
//}}}
//{{{
int main (int argc, char* argv[]) {

  WNDCLASS wndclass;
  wndclass.style         = 0;
  wndclass.lpfnWndProc   = (WNDPROC)WndProc;
  wndclass.cbClsExtra    = 0;
  wndclass.cbWndExtra    = 0;
  wndclass.hInstance     = GetModuleHandle (0);
  wndclass.hIcon         = LoadIcon (GetModuleHandle(0), szAppName);
  wndclass.hCursor       = LoadCursor (NULL,IDC_ARROW);
  wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  wndclass.lpszMenuName  = szAppName;
  wndclass.lpszClassName = szAppName;
  if (!RegisterClass (&wndclass) )
    return FALSE;

  hWnd = CreateWindow (szAppName, "Generic OpenGL Sample",
                       WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                       CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT,
                       NULL, NULL, GetModuleHandle (0), NULL);
  ShowWindow (hWnd, SW_SHOWDEFAULT);
  UpdateWindow (hWnd);

  GLreport();

  MSG msg;
  while (true) {
    while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) == TRUE) {
      if (GetMessage(&msg, NULL, 0, 0) ) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        }
      else
        return TRUE;
      }

    drawScene();
    }
  }
//}}}
