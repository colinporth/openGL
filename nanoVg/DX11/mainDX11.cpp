// D3D Demo port cmaughan.
//{{{  includes
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>

#define INITGUID
#include "nanoVgDX11.h"

#include "../demo.h"
#include "../perf.h"

#include <Windows.h>
#include <windowsx.h>

#pragma comment(lib,"D3D11.lib")
//}}}
//#define DEMO_MSAA

// d3d vars
ID3D11Device* pDevice;
D3D_FEATURE_LEVEL FeatureLevel;
ID3D11DeviceContext* pDeviceContext;

IDXGISwapChain* pSwapChain;
DXGI_SWAP_CHAIN_DESC swapDesc;
ID3D11RenderTargetView* pRenderTargetView;

ID3D11Texture2D* pDepthStencil;
ID3D11DepthStencilView* pDepthStencilView;

// window vars
HINSTANCE hInst;
HWND hWndMain = 0;
const char* pszWindowClass = "WindowClass";

int xm = 0;
int ym = 0;
float xWin = 0;
float yWin = 0;

struct NVGcontext* vg = NULL;
struct PerfGraph fps;
struct PerfGraph cpuGraph;
double prevt = 0;
double cpuTime = 0;
struct DemoData data;

int blowup = 0;
int premult = 0;
double cpuTimeResolution;
unsigned __int64 cpuTimeBase;

//{{{
void initCPUTimer() {

  unsigned __int64 frequency;
if (QueryPerformanceFrequency((LARGE_INTEGER*)&frequency))
    cpuTimeResolution = 1.0 / (double)frequency;

  QueryPerformanceCounter((LARGE_INTEGER*)&cpuTimeBase);
 }
//}}}
//{{{
double getCPUTime() {
  unsigned __int64 time;
  QueryPerformanceCounter((LARGE_INTEGER*)&time);
  return (double)(time - cpuTimeBase) * cpuTimeResolution;
  }
//}}}

//{{{
HRESULT resizeWindow (unsigned int x, unsigned int y) {

  xWin = (float)x;
  yWin = (float)y;

  if (!pDevice || !pDeviceContext)
    return E_FAIL;

  //pDeviceContext->ClearState();
  ID3D11RenderTargetView* viewList[1] = { NULL };
  pDeviceContext->OMSetRenderTargets (1, viewList, NULL);

  // Ensure that nobody is holding onto one of the old resources
  D3D_API_RELEASE (pRenderTargetView);
  D3D_API_RELEASE (pDepthStencilView);

  // Resize render target buffers
  HRESULT hr = pSwapChain->ResizeBuffers (1, x, y, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
  if (FAILED(hr))
    return hr;

  // Create the render target view and set it on the device
  ID3D11Resource* pBackBufferResource = NULL;
  hr = pSwapChain->GetBuffer (0, IID_ID3D11Texture2D, (void**)&pBackBufferResource);
  if (FAILED(hr))
    return hr;

  D3D11_RENDER_TARGET_VIEW_DESC renderDesc;
  renderDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  renderDesc.ViewDimension = (swapDesc.SampleDesc.Count > 1) ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
  renderDesc.Texture2D.MipSlice = 0;
  hr = pDevice->CreateRenderTargetView (pBackBufferResource, &renderDesc, &pRenderTargetView);
  D3D_API_RELEASE(pBackBufferResource);
  if (FAILED(hr))
    return hr;

  D3D11_TEXTURE2D_DESC texDesc;
  texDesc.ArraySize = 1;
  texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  texDesc.CPUAccessFlags = 0;
  texDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  texDesc.Height = (UINT)y;
  texDesc.Width = (UINT)x;
  texDesc.MipLevels = 1;
  texDesc.MiscFlags = 0;
  texDesc.SampleDesc.Count = swapDesc.SampleDesc.Count;
  texDesc.SampleDesc.Quality = swapDesc.SampleDesc.Quality;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  D3D_API_RELEASE (pDepthStencil);
  hr = pDevice->CreateTexture2D (&texDesc, NULL, &pDepthStencil);
  if (FAILED(hr))
    return hr;

  D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc;
  depthViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depthViewDesc.ViewDimension = (swapDesc.SampleDesc.Count>1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
  depthViewDesc.Flags = 0;
  depthViewDesc.Texture2D.MipSlice = 0;
  hr = pDevice->CreateDepthStencilView ((ID3D11Resource*)pDepthStencil, &depthViewDesc, &pDepthStencilView);

  return hr;
  }
//}}}
//{{{
void unInitializeDX() {

  // Detach RTs
  if (pDeviceContext) {
    ID3D11RenderTargetView *viewList[1] = { NULL };
    pDeviceContext->OMSetRenderTargets (1, viewList, NULL);
    }

  D3D_API_RELEASE (pDeviceContext);
  D3D_API_RELEASE (pDevice);
  D3D_API_RELEASE (pSwapChain);
  D3D_API_RELEASE (pRenderTargetView);
  D3D_API_RELEASE (pDepthStencil);
  D3D_API_RELEASE (pDepthStencilView);
  }
//}}}
//{{{
BOOL initializeDX (unsigned int x, unsigned int y) {
// Setup the device and the rendering targets

  static const D3D_FEATURE_LEVEL levelAttempts[] = {
    //D3D_FEATURE_LEVEL_12_0,
    //D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,  // Direct3D 11.0 SM 5
    D3D_FEATURE_LEVEL_10_1,  // Direct3D 10.1 SM 4
    D3D_FEATURE_LEVEL_10_0,  // Direct3D 10.0 SM 4
    D3D_FEATURE_LEVEL_9_3,   // Direct3D 9.3  SM 3
    D3D_FEATURE_LEVEL_9_2,   // Direct3D 9.2  SM 2
    D3D_FEATURE_LEVEL_9_1,   // Direct3D 9.1  SM 2
    };

  HRESULT hr = S_OK;
  UINT deviceFlags = 0;
  hr = D3D11CreateDevice (NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, deviceFlags,
                          levelAttempts, ARRAYSIZE(levelAttempts),
                          D3D11_SDK_VERSION, &pDevice, &FeatureLevel, &pDeviceContext );
  printf ("FeatureLevel:%x\n", FeatureLevel);
  if (FAILED(hr))
    return FALSE;

  IDXGIDevice* pDXGIDevice = NULL;
  hr = pDevice->QueryInterface (IID_IDXGIDevice, (void**)&pDXGIDevice);
  if (FAILED(hr))
    return FALSE;

  IDXGIAdapter* pAdapter = NULL;
  hr = pDXGIDevice->GetAdapter(&pAdapter);
  if (FAILED(hr))
    return FALSE;

  IDXGIFactory* pDXGIFactory = NULL;
  hr = pAdapter->GetParent (IID_IDXGIFactory, (void**)&pDXGIFactory);
  if (FAILED(hr))
    return FALSE;

  ZeroMemory (&swapDesc, sizeof (swapDesc));
  swapDesc.SampleDesc.Count = 1;   //The Number of Multisamples per Level
  swapDesc.SampleDesc.Quality = 0; //between 0(lowest Quality) and one lesser than pDevice->CheckMultisampleQualityLevels

  // Enable if you want to use multisample AA for the rendertarget
#ifdef DEMO_MSAA
  for (int i = 1; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i *= 2) {
  //for (int i = 1; i <= 4; i *= 2) {
    UINT Quality;
    if SUCCEEDED (pDevice->CheckMultisampleQualityLevels (DXGI_FORMAT_B8G8R8A8_UNORM, i, &Quality)) {
      if (Quality > 0) {
        printf("has quality %d %d\n", i, Quality);
        swapDesc.SampleDesc.Count = i;
        swapDesc.SampleDesc.Quality = Quality - 1;
        }
      else
        break;
      }
    }
#endif

  swapDesc.BufferDesc.Width = x;
  swapDesc.BufferDesc.Height = y;
  swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swapDesc.BufferDesc.RefreshRate.Numerator = 60;
  swapDesc.BufferDesc.RefreshRate.Denominator = 1;
  swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDesc.BufferCount = 1;
  swapDesc.OutputWindow = hWndMain;
  swapDesc.Windowed = TRUE;
  hr = pDXGIFactory->CreateSwapChain ((IUnknown*)pDevice, &swapDesc, &pSwapChain);
  if (FAILED(hr))
    return FALSE;

  D3D_API_RELEASE (pDXGIDevice);
  D3D_API_RELEASE (pAdapter);
  D3D_API_RELEASE (pDXGIFactory);

  if (SUCCEEDED(hr)) {
    hr = resizeWindow (x, y);
    if (FAILED(hr))
      return FALSE;
    }
  else {
    // Fail
    unInitializeDX();
    return FALSE;
    }

  return TRUE;
  }
//}}}

//{{{
void draw() {

  if (!pDeviceContext)
    return;

  double t = getCPUTime();
  double dt = t - prevt;
  prevt = t;

  pDeviceContext->OMSetRenderTargets (1, &pRenderTargetView, pDepthStencilView);

  D3D11_VIEWPORT viewport;
  viewport.Height = (float)yWin;
  viewport.Width = (float)xWin;
  viewport.MaxDepth = 1.0f;
  viewport.MinDepth = 0.0f;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  pDeviceContext->RSSetViewports (1, &viewport);

  float clearColor[4];
  if (premult) {
    clearColor[0] = 0.0f;
    clearColor[1] = 0.0f;
    clearColor[2] = 0.0f;
    clearColor[3] = 0.0f;
    }
  else {
    clearColor[0] = 0.3f;
    clearColor[1] = 0.3f;
    clearColor[2] = 0.32f;
    clearColor[3] = 1.0f;
    }
  pDeviceContext->ClearRenderTargetView (pRenderTargetView, clearColor);

  pDeviceContext->ClearDepthStencilView (
    pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, (UINT8)0);

  nvgBeginFrame (vg, xWin, yWin, 1.0f);
  renderDemo (vg, (float)xm, (float)ym, (float)xWin, (float)yWin, (float)t, blowup, &data);
  renderGraph (vg, 5, 5, &fps);
  renderGraph (vg, 5+200+5, 5, &cpuGraph);
  nvgEndFrame (vg);

  // Measure the CPU time taken excluding swap buffers (as the swap may wait for GPU)
  cpuTime = getCPUTime() - t;

  updateGraph (&fps, (float)dt);
  updateGraph (&cpuGraph, (float)cpuTime);

  // Don't wait for VBlank
  pSwapChain->Present (0, 0);
  }
//}}}

//{{{
LRESULT CALLBACK WndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

  switch (message) {
    case WM_KEYDOWN:
      if (GetKeyState (VK_ESCAPE))
        CloseWindow (hWnd);
      else if (GetKeyState (VK_SPACE))
        blowup = !blowup;
      else if (wParam == 'P')
        premult = !premult;
      break;

    case WM_MOUSEMOVE:
      xm = GET_X_LPARAM (lParam);
      ym = GET_Y_LPARAM (lParam);
      break;

    case WM_PAINT:
      draw();
      ValidateRect (hWnd, NULL);
      break;

    case WM_SIZE:
      resizeWindow (LOWORD(lParam), HIWORD(lParam));
      break;

    case WM_ERASEBKGND:
      return 1;
      break;

    case WM_DESTROY:
      unInitializeDX();
      PostQuitMessage(0);
      break;

    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
    }

  return 0;
  }
//}}}
//{{{
ATOM MyRegisterClass (HINSTANCE hInstance) {

  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style      = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wcex.lpfnWndProc  = WndProc;
  wcex.cbClsExtra   = 0;
  wcex.cbWndExtra   = 0;
  wcex.hInstance    = hInstance;
  wcex.hIcon      = NULL;
  wcex.hCursor    = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
  wcex.lpszMenuName = "";
  wcex.lpszClassName  = pszWindowClass;
  wcex.hIconSm    = NULL;

  return RegisterClassEx(&wcex);
  }
//}}}
//{{{
BOOL InitInstance (HINSTANCE hInstance, int nCmdShow) {

  hInst = hInstance; // Store instance handle in our global variable

  RECT rcWin;
  rcWin.left = 0;
  rcWin.right = 1000;
  rcWin.top = 0;
  rcWin.bottom = 600;
  AdjustWindowRectEx (&rcWin, WS_OVERLAPPEDWINDOW, FALSE, 0);

  rcWin.right += -rcWin.left;
  rcWin.bottom += -rcWin.top;
  hWndMain = CreateWindowEx (0, pszWindowClass, "nanoVgDX11", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, (int)rcWin.right, (int)rcWin.bottom,
                             NULL, NULL, hInstance, NULL);
  if (!hWndMain)
      return FALSE;

  if (FAILED (initializeDX (rcWin.right, rcWin.bottom))) {
    printf ("Could not init DX\n");
    return FALSE;
    }

#ifdef DEMO_MSAA
  vg = nvgCreateD3D11 (pDevice, NVG_STENCIL_STROKES);
#else
  vg = nvgCreateD3D11 (pDevice, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#endif
  if (vg == NULL) {
    printf ("Could not init nanovg.\n");
    return FALSE;
    }

  if (loadDemoData (vg, &data) == -1)
    return FALSE;

  initCPUTimer();

  initGraph (&fps, GRAPH_RENDER_FPS, "Frame Time");
  initGraph (&cpuGraph, GRAPH_RENDER_MS, "CPU Time");

  InvalidateRect (hWndMain, NULL, TRUE);
  ShowWindow (hWndMain, nCmdShow);
  UpdateWindow (hWndMain);

  return TRUE;
  }
//}}}

//{{{
int main() {

  hInst = GetModuleHandle (NULL);
  MyRegisterClass (hInst);

  if (!InitInstance (hInst, SW_SHOW))
    return FALSE;

  // Main message loop:
  MSG msg;
  ZeroMemory (&msg, sizeof(msg));
  while (msg.message != WM_QUIT) {
    if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
      }
    else
      draw();
    }

  freeDemoData (vg, &data);
  nvgDeleteD3D11 (vg);

  return (int)msg.wParam;
  }
//}}}
