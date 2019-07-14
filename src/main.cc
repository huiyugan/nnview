/*
MIT License

Copyright (c) 2019 Light Transport Entertainment Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "glad/glad.h"  // it looks its important to include glad before glfw

#include "GLFW/glfw3.h"

// imgui
#include "imgui.h"
#include "imgui_internal.h"

#include "examples/imgui_impl_glfw.h"
#include "examples/imgui_impl_opengl2.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "colormap.hh"
#include "io/weights-loader.hh"
#include "nnview_app.hh"

static void gui_new_frame() {
  glfwPollEvents();
  ImGui_ImplOpenGL2_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

static void gl_new_frame(GLFWwindow* window, ImVec4 clear_color, int* display_w,
                         int* display_h) {
  // Rendering
  glfwGetFramebufferSize(window, display_w, display_h);
  glViewport(0, 0, *display_w, *display_h);
  glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
  glEnable(GL_DEPTH_TEST);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBlendEquation(GL_FUNC_ADD);
}

static void gl_gui_end_frame(GLFWwindow* window) {
  glUseProgram(0);

  ImGui::Render();
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

  glfwSwapBuffers(window);
  glFlush();

  static int frameCount = 0;
  static double currentTime = glfwGetTime();
  static double previousTime = currentTime;
  static char title[256];

  frameCount++;
  currentTime = glfwGetTime();
  const auto deltaTime = currentTime - previousTime;
  if (deltaTime >= 1.0) {
    sprintf(title, "nnview GUI [%dFPS]", frameCount);
    glfwSetWindowTitle(window, title);
    frameCount = 0;
    previousTime = currentTime;
  }
}

#if 0
static bool ImGuiCombo(const char* label, int* current_item,
                       const std::vector<std::string>& items) {
  return ImGui::Combo(
      label, current_item,
      [](void* data, int idx_i, const char** out_text) {
        size_t idx = static_cast<size_t>(idx_i);
        const std::vector<std::string>* str_vec =
            reinterpret_cast<std::vector<std::string>*>(data);
        if (idx_i < 0 || str_vec->size() <= idx) {
          return false;
        }
        *out_text = str_vec->at(idx).c_str();
        return true;
      },
      reinterpret_cast<void*>(const_cast<std::vector<std::string>*>(&items)),
      static_cast<int>(items.size()), static_cast<int>(items.size()));
}
#endif

static void error_callback(int error, const char* description) {
  std::cerr << "GLFW Error : " << error << ", " << description << std::endl;
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action,
                         int mods) {
  (void)scancode;

  ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureKeyboard) {
    return;
  }

  if (key == GLFW_KEY_Q && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL)) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }
}

static void initialize_glfw_opengl_window(GLFWwindow*& window) {
  // Setup window
  glfwSetErrorCallback(error_callback);
  if (!glfwInit()) {
    exit(EXIT_FAILURE);
  }

#ifdef _DEBUG
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

#ifdef __APPLE__
  // Create OpenGL 3.3 context(this should be available Maveriks or later)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,
                 GL_TRUE);  // It looks this is important on macOS.
#endif
  window = glfwCreateWindow(1200, 800, "nnview", nullptr, nullptr);
  glfwMakeContextCurrent(window);

  glfwSwapInterval(1);  // Enable vsync
  glfwSetKeyCallback(window, key_callback);

  // glad must be called after glfwMakeContextCurrent()
  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::cerr << "Failed to load OpenGL functions with gladLoadGL\n";
    exit(EXIT_FAILURE);
  }

  std::cout << "OpenGL " << GLVersion.major << '.' << GLVersion.minor << '\n';

  std::cout << "GL_VENDOR : " << glGetString(GL_VENDOR) << "\n";
  std::cout << "GL_RENDERER : " << glGetString(GL_RENDERER) << "\n";
  std::cout << "GL_VERSION : " << glGetString(GL_VERSION) << "\n";

  if (GLVersion.major < 2) {
    std::cerr << "OpenGL 2.x is not available." << std::endl;
    exit(EXIT_FAILURE);
  }

#ifdef _DEBUG
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(reinterpret_cast<GLDEBUGPROC>(glDebugOutput), nullptr);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                        GL_TRUE);
#endif

  // glEnable(GL_BLEND);
  // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // glFrontFace(GL_CW);

  // load a window icon
  // load_and_set_window_icons(window);
}

static void initialize_imgui(GLFWwindow* window) {
  // Setup Dear ImGui context
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();

  // Enable docking(available in imgui `docking` branch at the moment)
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

#if 0
  const float default_font_scale = 16.f;
  ImFontConfig roboto_config;
  strcpy(roboto_config.Name, "Roboto");
  roboto_config.SizePixels = default_font_scale;
  roboto_config.OversampleH = 2;
  roboto_config.OversampleV = 2;
  io.Fonts->AddFontFromMemoryCompressedTTF(roboto_compressed_data,
                                           roboto_compressed_size,
                                           default_font_scale, &roboto_config);

  ImFontConfig ionicons_config;
  ionicons_config.MergeMode = true;
  ionicons_config.GlyphMinAdvanceX = default_font_scale;
  ionicons_config.OversampleH = 1;
  ionicons_config.OversampleV = 1;
  static const ImWchar icon_ranges[] = {ICON_MIN_II, ICON_MAX_II, 0};
  io.Fonts->AddFontFromMemoryCompressedTTF(
      ionicons_compressed_data, ionicons_compressed_size, default_font_scale,
      &ionicons_config, icon_ranges);

  ImFontConfig roboto_mono_config;
  strcpy(roboto_mono_config.Name, "Roboto Mono");
  roboto_mono_config.SizePixels = default_font_scale;
  roboto_mono_config.OversampleH = 2;
  roboto_mono_config.OversampleV = 2;
  io.Fonts->AddFontFromMemoryCompressedTTF(
      roboto_mono_compressed_data, roboto_compressed_size, default_font_scale,
      &roboto_mono_config);

  ImGuiFileDialog::fileLabel = ICON_II_ANDROID_DOCUMENT;
  ImGuiFileDialog::dirLabel = ICON_II_ANDROID_FOLDER;
  ImGuiFileDialog::linkLabel = ICON_II_ANDROID_ARROW_FORWARD;
#endif

  // Setup Platform/Renderer bindings
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL2_Init();

#ifndef FORCE_DEFAULT_STYLE
  // Setup Style
  // I took that nice dark color scheme from Derydoca as a base:
  // https://github.com/ocornut/imgui/issues/707?ts=4#issuecomment-463758243
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 1.0f;
  ImGui::GetStyle().ChildRounding = 1.0f;
  ImGui::GetStyle().FrameRounding = 1.0f;
  ImGui::GetStyle().GrabRounding = 1.0f;
  ImGui::GetStyle().PopupRounding = 1.0f;
  ImGui::GetStyle().ScrollbarRounding = 1.0f;
  ImGui::GetStyle().TabRounding = 1.0f;
  ImGui::GetStyle().FrameBorderSize = 2.f;
  ImGui::GetStyle().ScrollbarSize = 18.f;

  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.500f, 0.500f, 0.500f, 1.000f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.180f, 0.180f, 0.180f, 0.9f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.280f, 0.280f, 0.280f, 0.007843f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
  colors[ImGuiCol_Border] = ImVec4(0.266f, 0.266f, 0.266f, 1.000f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.200f, 0.200f, 0.200f, 1.000f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.280f, 0.280f, 0.280f, 1.000f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.277f, 0.277f, 0.277f, 1.000f);
  colors[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(0.300f, 0.300f, 0.300f, 1.000f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_CheckMark] = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_Button] = ImVec4(1.000f, 1.000f, 1.000f, 0.000f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
  colors[ImGuiCol_ButtonActive] = ImVec4(1.000f, 1.000f, 1.000f, 0.391f);
  colors[ImGuiCol_Header] = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
  colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(1.000f, 1.000f, 1.000f, 0.250f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.670f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_Tab] = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.352f, 0.352f, 0.352f, 1.000f);
  colors[ImGuiCol_TabActive] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
  // TODO see docking branch of ImGui
  colors[ImGuiCol_DockingPreview] = ImVec4(1.000f, 0.391f, 0.000f, 0.781f);
  colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.180f, 0.180f, 0.180f, 0.9f);
  colors[ImGuiCol_PlotLines] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
  colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_PlotHistogram] = ImVec4(0.586f, 0.586f, 0.586f, 1.000f);
  colors[ImGuiCol_PlotHistogramHovered] =
      ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
  colors[ImGuiCol_DragDropTarget] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_NavHighlight] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_NavWindowingHighlight] =
      ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);
#endif
}

static void deinitialize_gui_and_window(GLFWwindow* window) {
  // Cleanup
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
}

static void mouse_button_callback(GLFWwindow* window, int button, int action,
                                  int mods) {
  (void)mods;

  auto* param =
      &(reinterpret_cast<nnview::app*>(glfwGetWindowUserPointer(window))
            ->gui_parameters);

  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    param->button_states[0] = true;
  if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS)
    param->button_states[1] = true;
  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    param->button_states[2] = true;
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_FALSE)
    param->button_states[0] = false;
  if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_FALSE)
    param->button_states[1] = false;
  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_FALSE)
    param->button_states[2] = false;
}

static void cursor_pos_callback(GLFWwindow* window, double mouse_x,
                                double mouse_y) {
  auto* param =
      &(reinterpret_cast<nnview::app*>(glfwGetWindowUserPointer(window))
            ->gui_parameters);

  // mouse left pressed
  if (param->button_states[0] && !ImGui::GetIO().WantCaptureMouse) {
    param->rot_yaw -= param->rotation_scale * (mouse_x - param->last_mouse_x);
    param->rot_pitch -= param->rotation_scale * (mouse_y - param->last_mouse_y);
    param->rot_pitch = std::min(90.0, std::max(-90.0, param->rot_pitch));
  }

  param->last_mouse_x = mouse_x;
  param->last_mouse_y = mouse_y;
}

static void drop_callabck(GLFWwindow* window, int nums, const char** paths) {
  if (nums > 0) {
    // TODO(LTE): Do we need a lock?
    nnview::app* app =
        reinterpret_cast<nnview::app*>(glfwGetWindowUserPointer(window));

    // TODO(LTE): T.B.W.
    (void)paths;
    (void)app;
  }
}

static void show_tensor_value(
  // offset of Tensor Image widget from the window upper left location
  // This offset includes scroll factor.
  const ImVec2 tensor_image_widget_offset,
  const float alpha,
  const float step, const nnview::Tensor& tensor) {

  (void)step;
  (void)tensor;

  const float left_margin = 6.0f;
  const float top_margin = 6.0f;

  const float cell_left_margin = std::max(0.0f, step / 2.0f - 24.0f);
  const float cell_top_margin = std::max(0.0f, step / 2.0f - 10.0f);

  const size_t height = size_t(tensor.shape[0]);
  const size_t width = size_t(tensor.shape[1]);

  ImVec2 window_size = ImGui::GetWindowSize();

  size_t num_rect_draws = 0;

  // Draw values for only visible area
  for (size_t y = 0; y < height; y++) {
    // TODO(LTE): Compute bound outside of for loop.
    if (((step + 1) * y) < -tensor_image_widget_offset.y) {
      continue;
    }
    if (((step * y ) > (-tensor_image_widget_offset.y + window_size.y))) {
      continue;
    }

    for (size_t x = 0; x < width; x++) {

      // TODO(LTE): Compute bound outside of for loop.
      if ((step + 1) * x < -tensor_image_widget_offset.x) {
        continue;
      }

      if ((step * x) > (-tensor_image_widget_offset.x + window_size.x)) {
        continue;
      }

      const float value = tensor.data[y * width + x];

      char buf[64];
      snprintf(buf, sizeof(buf), "%4.3f", double(value));

      ImVec2 bmin =
        ImVec2(tensor_image_widget_offset.x + step * x + left_margin + cell_left_margin,
               tensor_image_widget_offset.y + step * y + top_margin + cell_top_margin);

      // Prevent too many AddRectFilled call for safety.
      // ImGui's default uses 16bit indices, so drawing too many rects will
      // cause the assertion failure inside imgui.
      // 1024 = heuristic value.
      if (num_rect_draws < 1024) {
        ImVec2 text_size = ImGui::CalcTextSize(buf);

        ImVec2 fill_bmin = ImVec2(bmin.x - 4, bmin.y - 4);
        ImVec2 fill_bmax = ImVec2(bmin.x + text_size.x + 4, bmin.y + text_size.y + 4);

        // Draw quad for background color
        ImGui::GetWindowDrawList()->AddRectFilled(fill_bmin, fill_bmax, ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 0.4f * alpha)), /* rounding */4.0f);

        num_rect_draws++;
      }

      ImGui::SetCursorScreenPos(bmin);

      ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, alpha), "%s", buf);
    }
  }
}

static void tensor_window(const GLuint texid, const nnview::Tensor& tensor) {
  ImGui::Begin("Tensor", /* p_open */ nullptr,
               ImGuiWindowFlags_HorizontalScrollbar);

  ImGui::Text("Tensor : %s", tensor.name.c_str());

  static float scale = 4.0f;  // Set 4x for better initial visual
  ImGui::SliderFloat("scale", &scale, 0.0f, 100.0f);

  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 win_pos = ImGui::GetWindowPos();

  ImGui::Text("win pos %f, %f", double(win_pos.x), double(win_pos.y));
  ImGui::Text("cur pos %f, %f", double(pos.x), double(pos.y));


  // Store base corner position of the image for pixel value text overlay
  ImVec2 image_pos = ImGui::GetCursorScreenPos();

  ImGui::Image(ImTextureID(intptr_t(texid)),
               ImVec2(scale * tensor.shape[1], scale * tensor.shape[0]));

  if (scale > 40.0f) {

    // 40.0 ~ 64.0 : alpha 0 -> 1
    // 64.0 > : 1
    const float alpha =
        (scale > 64.0f) ? 1.0f : (scale - 40.0f) / (64.0f - 40.0f);
    show_tensor_value(image_pos, alpha, scale, tensor);
  }

  ImGui::End();
}

inline uint8_t ftoc(const float x) {
  int i = int(x * 255.0f);
  i = std::min(255, std::max(0, i));
  return uint8_t(i);
}

static std::vector<uint8_t> tensor_to_color(const nnview::Tensor& tensor) {
  std::vector<uint8_t> img;
  img.resize(size_t(tensor.shape[0] * tensor.shape[1] * 4));

  // find max/min value
  float min_value = std::numeric_limits<float>::max();
  float max_value = -std::numeric_limits<float>::max();

  for (size_t i = 0; i < size_t(tensor.shape[0] * tensor.shape[1]); i++) {
    min_value = std::min(min_value, tensor.data[i]);
    max_value = std::max(max_value, tensor.data[i]);
  }

  std::cout << "tensor min/max = " << min_value << ", " << max_value
            << std::endl;

  for (size_t i = 0; i < size_t(tensor.shape[0] * tensor.shape[1]); i++) {
    // normalize.
    const float x = (tensor.data[i] - min_value) / (max_value - min_value);
    nnview::vec3 rgb = nnview::viridis(x);

    // std::cout << rgb[0] << ", " << rgb[1] << ", " << rgb[2] << std::endl;

    img[4 * i + 0] = ftoc(rgb[0]);
    img[4 * i + 1] = ftoc(rgb[1]);
    img[4 * i + 2] = ftoc(rgb[2]);
    img[4 * i + 3] = 255;
  }

  return img;
}

static GLuint gen_texture(const nnview::Tensor& tensor) {
  GLuint texid = 0;
  glGenTextures(1, &texid);

  glBindTexture(GL_TEXTURE_2D, texid);

  std::vector<uint8_t> img = tensor_to_color(tensor);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tensor.shape[1], tensor.shape[0],
               /* border */ 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());

  // No bilinear filtering.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, 0);

  return texid;
}

#if 0
static void update_texture(GLuint texid, const nnview::Tensor& tensor) {

  std::vector<uint8_t> img = tensor_to_color(tensor);

  glBindTexture(GL_TEXTURE_2D, texid);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tensor.shape[1], tensor.shape[0],
                  GL_RGBA, GL_FLOAT, img.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}
#endif

int main(int argc, char** argv) {
  // TODO(LTE): Parse args.
  (void)argc;
  (void)argv;

  if (argc < 2) {
    std::cerr << "Need input.weights\n";
    return EXIT_FAILURE;
  }

  nnview::Tensor tensor;

  {
    bool ret = nnview::load_weights(argv[1], &tensor);
    if (!ret) {
      std::cerr << "Failed to read weights : " << argv[1] << "\n";
      return EXIT_FAILURE;
    }
  }

  GLFWwindow* window = nullptr;
  nnview::app app;

  initialize_glfw_opengl_window(window);
  // glfwSetWindowUserPointer(window, &gui_parameters);
  glfwSetWindowUserPointer(window, &app);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetCursorPosCallback(window, cursor_pos_callback);

  // NOTE: We cannot use lambda function with [this] capture, so pass `this`
  // pointer through glfwSetWindowUserPointer.
  // https://stackoverflow.com/questions/39731561/use-lambda-as-glfwkeyfun
  glfwSetDropCallback(window, drop_callabck);

  initialize_imgui(window);
  (void)ImGui::GetIO();

  // Setup texture for Tensor display
  GLuint tensor_texid = gen_texture(tensor);

  ImVec4 background_color = ImVec4(0.05f, 0.05f, 0.08f, 1.00f);

  while (!glfwWindowShouldClose(window)) {
    gui_new_frame();
    int display_w, display_h;
    gl_new_frame(window, background_color, &display_w, &display_h);

    tensor_window(tensor_texid, tensor);

    // update_texture(tensor_texid, tensor);

    // Render all ImGui, then swap buffers
    gl_gui_end_frame(window);
  }

  deinitialize_gui_and_window(window);

  return EXIT_SUCCESS;
}
