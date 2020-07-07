// fboMain.cpp
//{{{  includes
#include <stdio.h>

#define GLEW_STATIC
#include <GLEW/glew.h>
#include <GLFW/glfw3.h>

#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg.h"
#include "nanoVgGL.h"
#include "nanoVgGLutils.h"

#include "perf.h"

#pragma comment(lib,"openGL32.lib")
#pragma comment(lib,"glfw3.lib")
#pragma comment(lib,"glew32s.lib")
//}}}

//{{{
void errorcb (int error, const char* desc) {
  printf ("GLFW error %d: %s\n", error, desc);
  }
//}}}
//{{{
void renderPattern (NVGcontext* vg, NVGLUframebuffer* fb, float t, float pxRatio) {

  if (fb == NULL)
    return;

  float s = 20.0f;
  float sr = (cosf(t) + 1) * 0.5f;
  float r = s * 0.6f * (0.2f + 0.8f * sr);

  int fboWidth, fboHeight;
  nvgImageSize (vg, fb->image, &fboWidth, &fboHeight);
  int winWidth = (int)(fboWidth / pxRatio);
  int winHeight = (int)(fboHeight / pxRatio);

  // Draw some stuff to an FBO as a test
  nvgluBindFramebuffer (fb);
  glViewport (0, 0, fboWidth, fboHeight);
  glClearColor (0, 0, 0, 0);
  glClear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  nvgBeginFrame (vg, winWidth, winHeight, pxRatio);

  int pw = (int)ceilf (winWidth / s);
  int ph = (int)ceilf (winHeight / s);

  nvgBeginPath(vg);
  for (int y = 0; y < ph; y++) {
    for (int x = 0; x < pw; x++) {
      float cx = (x+0.5f) * s;
      float cy = (y+0.5f) * s;
      nvgCircle (vg, cx,cy, r);
      }
    }
  nvgFillColor (vg, nvgRGBA (220,160,0,200));
  nvgFill (vg);

  nvgEndFrame (vg);
  nvgluBindFramebuffer (NULL);
  }
//}}}
//{{{
int loadFonts (NVGcontext* vg) {

  int font = nvgCreateFont (vg, "sans", "../example/Roboto-Regular.ttf");
  if (font == -1) {
    printf ("Could not add font regular.\n");
    return -1;
    }

  font = nvgCreateFont (vg, "sans-bold", "../example/Roboto-Bold.ttf");
  if (font == -1) {
    printf ("Could not add font bold.\n");
    return -1;
    }

  return 0;
  }
//}}}
//{{{
static void key (GLFWwindow* window, int key, int scancode, int action, int mods) {
  NVG_NOTUSED (scancode);
  NVG_NOTUSED (mods);
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose (window, GL_TRUE);
  }
//}}}

int main() {

  GPUtimer gpuTimer;
  PerfGraph fps, cpuGraph, gpuGraph;
  double prevt = 0, cpuTime = 0;
  int winWidth, winHeight;
  int fbWidth, fbHeight;

  if (!glfwInit()) {
    printf ("Failed to init GLFW.");
    return -1;
    }

  initGraph (&fps, GRAPH_RENDER_FPS, "Frame Time");
  initGraph (&cpuGraph, GRAPH_RENDER_MS, "CPU Time");
  initGraph (&gpuGraph, GRAPH_RENDER_MS, "GPU Time");

  glfwSetErrorCallback (errorcb);
#ifndef _WIN32 // don't require this on win32, and works with more cards
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
  glfwWindowHint (GLFW_OPENGL_DEBUG_CONTEXT, 1);

  GLFWwindow* window = glfwCreateWindow (1000, 600, "NanoVG", NULL, NULL);
  //window = glfwCreateWindow(1000, 600, "NanoVG", glfwGetPrimaryMonitor(), NULL);
  if (!window) {
    glfwTerminate();
    return -1;
    }

  glfwSetKeyCallback (window, key);

  glfwMakeContextCurrent (window);
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    printf("Could not init glew.\n");
    return -1;
    }
  glGetError();

  NVGcontext* vg = nvgCreateGL3 (NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
  if (vg == NULL) {
    printf ("Could not init nanovg.\n");
    return -1;
    }

  // Create hi-dpi FBO for hi-dpi screens.
  glfwGetWindowSize (window, &winWidth, &winHeight);
  glfwGetFramebufferSize (window, &fbWidth, &fbHeight);

  // The image pattern is tiled, set repeat on x and y.
  NVGLUframebuffer* fb = nvgluCreateFramebuffer (
    vg, (int)(100*pxRatio), (int)(100*(float)fbWidth / (float)winWidth), NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
  if (fb == NULL) {
    printf ("Could not create FBO.\n");
    return -1;
    }

  if (loadFonts (vg) == -1) {
    printf ("Could not load fonts\n");
    return -1;
    }

  glfwSwapInterval (0);

  initGPUTimer (&gpuTimer);

  glfwSetTime (0);
  prevt = glfwGetTime();

  while (!glfwWindowShouldClose (window)) {
    double mx, my, t, dt;
    float gpuTimes[3];
    int i, n;

    t = glfwGetTime();
    dt = t - prevt;
    prevt = t;

    startGPUTimer (&gpuTimer);

    glfwGetCursorPos (window, &mx, &my);
    glfwGetWindowSize (window, &winWidth, &winHeight);
    glfwGetFramebufferSize (window, &fbWidth, &fbHeight);
    renderPattern (vg, fb, (float)t, (float)fbWidth / (float)winWidth);

    // Update and render
    glViewport (0, 0, fbWidth, fbHeight);
    glClearColor (0.3f, 0.3f, 0.32f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame (vg, winWidth, winHeight, pxRatio);

    // Use the FBO as image pattern.
    if (fb != NULL) {
      NVGpaint img = nvgImagePattern (vg, 0, 0, 100, 100, 0, fb->image, 1.0f);
      nvgSave (vg);

      for (i = 0; i < 20; i++) {
        nvgBeginPath (vg);
        nvgRect (vg, 10.0f + (float)i*30.0f, 10.0f, 10.0f, winHeight-20.0f);
        nvgFillColor (vg, nvgHSLA(i/19.0f, 0.5f, 0.5f, 255));
        nvgFill (vg);
        }

      nvgBeginPath (vg);
      nvgRoundedRect (vg, 140.0f + sinf((float)t*1.3f)*100.0f, 140.0f + cosf((float)t*1.71244f)*100.0f, 250.0f, 250.0f, 20.0f);
      nvgFillPaint (vg, img);
      nvgFill (vg);
      nvgStrokeColor (vg, nvgRGBA(220,160,0,255));
      nvgStrokeWidth (vg, 3.0f);
      nvgStroke (vg);

      nvgRestore (vg);
      }

    renderGraph (vg, 5,5, &fps);
    renderGraph (vg, 5+200+5,5, &cpuGraph);
    if (gpuTimer.supported)
      renderGraph (vg, 5+200+5+200+5,5, &gpuGraph);
    nvgEndFrame (vg);

    // Measure the CPU time taken excluding swap buffers (as the swap may wait for GPU)
    cpuTime = glfwGetTime() - t;

    updateGraph (&fps, (float)dt);
    updateGraph (&cpuGraph, (float)cpuTime);

    // We may get multiple results.
    n = stopGPUTimer (&gpuTimer, gpuTimes, 3);
    for (i = 0; i < n; i++)
      updateGraph (&gpuGraph, gpuTimes[i]);

    glfwSwapBuffers (window);
    glfwPollEvents();
    }

  nvgluDeleteFramebuffer (fb);
  nvgDeleteGL3 (vg);

  printf ("Average Frame Time: %.2f ms\n", getGraphAverage(&fps) * 1000.0f);
  printf ("          CPU Time: %.2f ms\n", getGraphAverage(&cpuGraph) * 1000.0f);
  printf ("          GPU Time: %.2f ms\n", getGraphAverage(&gpuGraph) * 1000.0f);

  glfwTerminate();
  return 0;
  }
