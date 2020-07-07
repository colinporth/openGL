// gl3.cpp
//{{{  includes
#include <stdio.h>

#define GLEW_STATIC
#include <GLEW/glew.h>
#include <GLFW/glfw3.h>

#define NANOVG_GL3_IMPLEMENTATION
#include "nanoVg.h"
#include "nanoVgGL.h"

#include "demo.h"
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
int blowup = 0;
int screenshot = 0;
int premult = 0;
//{{{
static void key (GLFWwindow* window, int key, int scancode, int action, int mods) {

  NVG_NOTUSED(scancode);
  NVG_NOTUSED(mods);

  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose (window, GL_TRUE);

  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    blowup = !blowup;

  if (key == GLFW_KEY_S && action == GLFW_PRESS)
    screenshot = 1;

  if (key == GLFW_KEY_P && action == GLFW_PRESS)
    premult = !premult;
  }
//}}}

int main() {

  if (!glfwInit()) {
    //{{{  error
    printf ("Failed to init GLFW.");
    return -1;
    }
    //}}}

  PerfGraph fps, cpuGraph, gpuGraph;
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

  auto window = glfwCreateWindow (1000, 600, "NanoVG", NULL, NULL);
  if (!window) {
    //{{{  error
    glfwTerminate();
    return -1;
    }
    //}}}
  glfwSetKeyCallback (window, key);
  glfwMakeContextCurrent (window);

  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    //{{{  error
    printf ("Could not init glew.\n");
    return -1;
    }
    //}}}
  // GLEW generates GL error because it calls glGetString(GL_EXTENSIONS), we'll consume it here.
  glGetError();

  auto vg = nvgCreateGL (NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
  if (vg == NULL) {
    //{{{  error
    printf ("Could not init nanovg\n");
    return -1;
    }
    //}}}

  DemoData data;
  if (loadDemoData (vg, &data) == -1)
    return -1;

  glfwSwapInterval (0);

  GPUtimer gpuTimer;
  initGPUTimer (&gpuTimer);
  glfwSetTime (0);

  auto prevt = glfwGetTime();
  double cpuTime = 0;
  while (!glfwWindowShouldClose (window)) {
    auto t = glfwGetTime();
    auto dt = t - prevt;
    prevt = t;

    startGPUTimer (&gpuTimer);

    double cursorX, cursorY;
    glfwGetCursorPos (window, &cursorX, &cursorY);
    int winWidth, winHeight;
    glfwGetWindowSize (window, &winWidth, &winHeight);
    int frameBufferWidth, frameBufferHeight;
    glfwGetFramebufferSize (window, &frameBufferWidth, &frameBufferHeight);

    // Update and render
    glViewport (0, 0, frameBufferWidth, frameBufferHeight);
    if (premult)
      glClearColor (0,0,0,0);
    else
      glClearColor (0.3f, 0.3f, 0.32f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    //printf ("%f %f %d %d\n", (float)cursorX, (float)cursorX, winWidth, winHeight);
    nvgBeginFrame (vg, winWidth, winHeight, (float)frameBufferWidth / (float)winWidth);
    renderDemo (vg, (float)cursorX, (float)cursorY, (float)winWidth, (float)winHeight, (float)t, blowup, &data);
    renderGraph (vg, 5, 5, &fps);
    renderGraph (vg, 5 + 200 + 5, 5, &cpuGraph);
    if (gpuTimer.supported)
      renderGraph (vg, 5 + 200 + 5 + 200 + 5, 5, &gpuGraph);
    nvgEndFrame (vg);

    // Measure the CPU time taken excluding swap buffers (as the swap may wait for GPU)
    cpuTime = glfwGetTime() - t;
    updateGraph (&fps, (float)dt);
    updateGraph (&cpuGraph, (float)cpuTime);

    // We may get multiple results.
    float gpuTimes[3];
    int n = stopGPUTimer (&gpuTimer, gpuTimes, 3);
    for (int i = 0; i < n; i++)
      updateGraph (&gpuGraph, gpuTimes[i]);

    if (screenshot) {
      screenshot = 0;
      saveScreenShot (frameBufferWidth, frameBufferHeight, premult, "dump.png");
      }

    glfwSwapBuffers (window);
    glfwPollEvents();
    }

  freeDemoData (vg, &data);
  nvgDeleteGL (vg);
  glfwTerminate();
  return 0;
  }
