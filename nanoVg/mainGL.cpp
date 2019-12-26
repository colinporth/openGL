// mainGL.cpp
//{{{  includes
#include <stdio.h>

#include "glad/glad.h"
#include <GLFW/glfw3.h>

//#define NANOVG_GL2_IMPLEMENTATION
//#define NANOVG_GL3_IMPLEMENTATION
//#define NANOVG_GLES2_IMPLEMENTATION
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanoVgGL.h"
//#include "nanoVg.h"

#include "demo.h"
#include "perf.h"

//}}}
//#define DEMO_MSAA

int blowup = 0;
int premult = 0;
//{{{
void errorcb (int error, const char* desc)
{
  printf("GLFW error %d: %s\n", error, desc);
}
//}}}
//{{{
static void key (GLFWwindow* window, int key, int scancode, int action, int mods) {

  NVG_NOTUSED (scancode);
  NVG_NOTUSED (mods);

  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose (window, GL_TRUE);

  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    blowup = !blowup;

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

  glfwSetErrorCallback (errorcb);

  glfwWindowHint (GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  //glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  //glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  //glfwWindowHint (GLFW_OPENGL_DEBUG_CONTEXT, 1);

  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  //glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 2);

  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 0);
  //glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 1);
  //glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);

#ifdef DEMO_MSAA
  glfwWindowHint (GLFW_SAMPLES, 4);
#endif

#ifdef _WIN32
  GLFWwindow* window = glfwCreateWindow (1000, 600, "NanoVG", NULL, NULL);
#else
  GLFWwindow* window = glfwCreateWindow (800, 600, "NanoVG", NULL, NULL);
#endif
  if (!window) {
    //{{{  error
    glfwTerminate();
    return -1;
    }
    //}}}
  glfwSetKeyCallback (window, key);
  glfwMakeContextCurrent (window);

  gladLoadGLLoader ((GLADloadproc)glfwGetProcAddress);

#ifdef DEMO_MSAA
  NVGcontext* vg = nvgCreateGL (NVG_STENCIL_STROKES | NVG_DEBUG);
#else
  NVGcontext* vg = nvgCreateGL (NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
#endif
  if (vg == NULL) {
    //{{{  error
    printf("Could not init nanovg.\n");
    return -1;
    }
    //}}}

  DemoData data;
  if (loadDemoData (vg, &data) == -1)
    //{{{  error
    return -1;
    //}}}

  glfwSwapInterval(0);
  glfwSetTime(0);
  double prevt = glfwGetTime();

  PerfGraph fps;
  initGraph (&fps, GRAPH_RENDER_FPS, "Frame Time");
  PerfGraph cpuGraph;
  initGraph (&cpuGraph, GRAPH_RENDER_MS, "CPU Time");
  while (!glfwWindowShouldClose (window)) {
    double t = glfwGetTime();
    double dt = t - prevt;
    prevt = t;

    double mx, my;
    glfwGetCursorPos (window, &mx, &my);

    int winWidth, winHeight;
    glfwGetWindowSize (window, &winWidth, &winHeight);

    int fbWidth, fbHeight;
    glfwGetFramebufferSize (window, &fbWidth, &fbHeight);

    // Update and render
    glViewport(0, 0, fbWidth, fbHeight);
    if (premult)
      glClearColor (0, 0 , 0, 0);
    else
      glClearColor (0.3f, 0.3f, 0.32f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // Calculate pixel ration for hi-dpi devices.
    float pxRatio = (float)fbWidth / (float)winWidth;
    nvgBeginFrame (vg, (float)winWidth, (float)winHeight, pxRatio);
    renderDemo (vg, (float)mx, (float)my, (float)winWidth, (float) winHeight, (float)t, blowup, &data);
    renderGraph (vg, 5,5, &fps);
    renderGraph (vg, 5 + 200 + 5, 5, &cpuGraph);
    nvgEndFrame (vg);

    auto cpuTime = glfwGetTime() - t;
    updateGraph (&fps, (float)dt);
    updateGraph (&cpuGraph, (float)cpuTime);

    glfwSwapBuffers (window);
    glfwPollEvents();
    }

  freeDemoData (vg, &data);
  nvgDeleteGL (vg);
  glfwTerminate();

  return 0;
  }
