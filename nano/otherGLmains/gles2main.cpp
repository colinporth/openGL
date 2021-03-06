// gles2.cpp
//{{{  includes
#include <stdio.h>

#define GLFW_INCLUDE_ES2
#include <GLFW/glfw3.h>

#define NANOVG_GLES2_IMPLEMENTATION
#include "nanoVg.h"
#include "nanoVgGL.h"
#include "nanoVgGLutils.h"

#include "demo.h"
#include "perf.h"

#pragma comment(lib,"opengl32.lib")
#pragma comment(lib,"libGLESv2.lib")
#pragma comment(lib,"glfw3.lib")
#pragma comment(lib,"glew32s.lib")
//}}}

int blowup = 0;
int screenshot = 0;
int premult = 0;
//{{{
void errorcb (int error, const char* desc)
{
  printf("GLFW error %d: %s\n", error, desc);
}
//}}}
//{{{
static void key (GLFWwindow* window, int key, int scancode, int action, int mods)
{
  NVG_NOTUSED(scancode);
  NVG_NOTUSED(mods);
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, GL_TRUE);
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    blowup = !blowup;
  if (key == GLFW_KEY_S && action == GLFW_PRESS)
    screenshot = 1;
  if (key == GLFW_KEY_P && action == GLFW_PRESS)
    premult = !premult;
}
//}}}

int main() {
  DemoData data;
  NVGcontext* vg = NULL;
  PerfGraph fps;
  double prevt = 0;

  if (!glfwInit()) {
    printf("Failed to init GLFW.");
    return -1;
    }

  initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");

  glfwSetErrorCallback(errorcb);

  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  GLFWwindow* window = glfwCreateWindow(1000, 600, "NanoVG", NULL, NULL);
  if (!window) {
    glfwTerminate();
    return -1;
    }

  glfwSetKeyCallback(window, key);

  glfwMakeContextCurrent(window);

  vg = nvgCreateGL (NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
  if (vg == NULL) {
    printf ("Could not init nanovg.\n");
    return -1;
    }

  if (loadDemoData(vg, &data) == -1)
    return -1;

  glfwSwapInterval (0);

  glfwSetTime(0);
  prevt = glfwGetTime();

  while (!glfwWindowShouldClose (window)) {
    double mx, my, t, dt;
    int winWidth, winHeight;
    int fbWidth, fbHeight;
    float pxRatio;

    t = glfwGetTime();
    dt = t - prevt;
    prevt = t;
    updateGraph(&fps, (float)dt);

    glfwGetCursorPos (window, &mx, &my);
    glfwGetWindowSize (window, &winWidth, &winHeight);
    glfwGetFramebufferSize (window, &fbWidth, &fbHeight);
    // Calculate pixel ration for hi-dpi devices.
    pxRatio = (float)fbWidth / (float)winWidth;

    // Update and render
    glViewport(0, 0, fbWidth, fbHeight);
    if (premult)
      glClearColor(0,0,0,0);
    else
      glClearColor(0.3f, 0.3f, 0.32f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    nvgBeginFrame(vg, winWidth, winHeight, pxRatio);

    renderDemo(vg, (float)mx, (float)my, (float)winWidth, (float)winHeight, (float)t, blowup, &data);
    renderGraph(vg, 5,5, &fps);

    nvgEndFrame(vg);

    if (screenshot) {
      screenshot = 0;
      saveScreenShot(fbWidth, fbHeight, premult, "dump.png");
      }

    glEnable(GL_DEPTH_TEST);

    glfwSwapBuffers(window);
    glfwPollEvents();
    }

  freeDemoData(vg, &data);

  nvgDeleteGL (vg);

  glfwTerminate();
  return 0;
  }
