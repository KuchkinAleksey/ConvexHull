#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

const float PI = 3.14159265358979f; 
const unsigned int kPointNodes = 72;
const unsigned int kSamples = 20;
const double kAnime = 0.1; // dt between solver steps
const unsigned int kWinWidth = 900;
const unsigned int kWinHeight = 900;
const std::string out_dir = "out";

const char *kVertexShaderSrc = R"(#version 330 core
layout (location = 0) in vec2 pos;

uniform vec4 color;

out vec4 inColor;

void main() {
  gl_Position = vec4(pos, 0.0, 1.0);
  inColor = color;
})";

const char *kFragmentShaderSrc = R"(#version 330 core
in vec4 inColor;
out vec4 FragColor;

void main() {
  FragColor = inColor;
})";

struct vec2f 
{
  float x,y;

  vec2f operator+(const vec2f& v) const { return vec2f{x+v.x,y+v.y}; }
  vec2f operator*(const float v) const { return vec2f{x*v,y*v}; }
  vec2f operator-(const vec2f& v) const { return vec2f{x-v.x,y-v.y}; }
  bool operator==(const vec2f& v) const { return std::abs(x-v.x) < 1e-9f && std::abs(y-v.y) < 1e-9f; }

  float norm() const {return std::sqrt(x*x+y*y); }
};
    
GLFWwindow* window;
std::vector<vec2f> points;
vec2f mean{0.0f,0.0f};
std::vector<vec2f> line_segments;
std::vector<float> vertices;
std::vector<float> vertices_d;
unsigned int VBO, VAO, VBOd, VAOd;
unsigned int shader_program;

void MakeWindow(unsigned int width, unsigned int height, const char *title) 
{
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 8);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  window = glfwCreateWindow(width, height, title, NULL, NULL);
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* mode = glfwGetVideoMode(monitor);
	glfwSetWindowPos(window, (mode->width-width)/2, (mode->height-height)/2);
  glfwMakeContextCurrent(window);

  gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
  glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
  glViewport(0, 0, width, height);
  glEnable(GL_MULTISAMPLE);

  unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &kVertexShaderSrc, NULL);
  glCompileShader(vertex_shader);

  unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &kFragmentShaderSrc, NULL);
  glCompileShader(fragment_shader);

  shader_program = glCreateProgram();
  glAttachShader(shader_program, vertex_shader);
  glAttachShader(shader_program, fragment_shader);
  glLinkProgram(shader_program);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
}

void DrawPoint(const vec2f &center, std::vector<float> &container) 
{
  const float r1 = 0.02f;
  const float r2 = 0.015f;
  vec2f A = center + vec2f{r1,0};
  vec2f B = center + vec2f{r2,0};
  for (unsigned int j=1; j<=kPointNodes; ++j)
  {
    const float ang = 2*PI*j/float(kPointNodes);
    vec2f dir = {std::cos(ang),std::sin(ang)};
    vec2f C = center + dir*r1;
    vec2f D = center + dir*r2;

    std::vector<float> line = {C.x,C.y,A.x,A.y,D.x,D.y,B.x,B.y};
    std::copy(line.begin(), line.end(), std::back_inserter(container));

    A = C;
    B = D;
  }
}

void DrawLine(const vec2f &pt1, const vec2f &pt2, std::vector<float> &container) 
{
  const float d = 0.01f;

  vec2f v = pt2-pt1;
  vec2f n = {-v.y,v.x};
  float norm = n.norm();
  if (norm == 0.0f) return;
  vec2f p = n*(d/(2.0f*norm));

  vec2f A = pt1 + p;
  vec2f B = pt1 - p;
  vec2f C = pt2 - p;
  vec2f D = pt2 + p;

  std::vector<float> line = {C.x,C.y,A.x,A.y,D.x,D.y,B.x,B.y};
  std::copy(line.begin(), line.end(), std::back_inserter(container));
}

void GenerateData() 
{
  points.reserve(kSamples);
  vertices.reserve(2*4*kPointNodes*kSamples);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.9f, 0.9f);
  for (unsigned int i=0; i<kSamples; ++i) 
  {
    vec2f pt = {dist(gen),dist(gen)};
    DrawPoint(pt, vertices);
    mean = mean + pt*(1.0f/float(kSamples));
    points.emplace_back(pt);
  }

  glBindVertexArray(VAO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, 0); 

  glBindVertexArray(0); 
}

bool SolverStep() 
{
  if (line_segments.size() > 1)
  {
    for (unsigned int idx = 0; idx < line_segments.size()-1; ++idx) 
      if (line_segments[line_segments.size()-1] == line_segments[idx]) 
      {
        if (idx == 0) return false;
        else {
          line_segments.erase(line_segments.begin(),line_segments.begin()+idx+1);
          break;
        }
      }
  }
  vertices_d.clear();
 
  if (line_segments.empty()) 
  {
    line_segments.emplace_back(points[0]);
  }
  else 
  {
    unsigned int lst =  unsigned int(line_segments.size())-1;
    vec2f v1 = line_segments[lst]-mean;
    unsigned int closest = 0;
    float min_ang = 360.0f;
    for (unsigned int idx = 0; idx < points.size(); ++idx)
    {
      const auto pt = points[idx];
      vec2f v2 = pt-line_segments[lst];
      if (v2.norm() == 0.0f) continue;
      float dot = v1.x*v2.x+v1.y*v2.y;
      float det = v1.x*v2.y-v1.y*v2.x;
      float ang = atan2f(det,dot)*180.0f/PI;
      if (ang < 0) ang = 180.f - ang;
      if (ang < min_ang)
      {
        min_ang = ang;
        closest = idx;
      }
    }
    if (min_ang < 90 && lst == 0) line_segments[lst] = points[closest];
    else line_segments.emplace_back(points[closest]);
  }

  for (unsigned int i = 0; i < line_segments.size(); ++i)
    DrawPoint(line_segments[i], vertices_d);
  DrawPoint(mean, vertices_d); 
  
  for (unsigned int i = 1; i < line_segments.size(); ++i)
    DrawLine(line_segments[i], line_segments[i-1], vertices_d);
  DrawLine(mean,line_segments[line_segments.size()-1],vertices_d);

  glBindVertexArray(VAOd);
  glBindBuffer(GL_ARRAY_BUFFER, VBOd);
  glBufferData(GL_ARRAY_BUFFER, vertices_d.size()*sizeof(float), vertices_d.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, 0); 

  glBindVertexArray(0); 

  return true;
}

int main()
{
  MakeWindow(kWinHeight, kWinWidth, "ConvexHull");

  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glGenVertexArrays(1, &VAOd);
  glGenBuffers(1, &VBOd);

  GenerateData();

  double prev_time = -kAnime;

  namespace fs = std::filesystem;
  fs::remove_all(out_dir);
  fs::create_directories(out_dir);

  int k = 0;
  int k_saved = 0;

  while (!glfwWindowShouldClose(window))
  {
    double current_time = glfwGetTime();
    if (current_time-kAnime >= prev_time) 
    {
      k += int(SolverStep());
      prev_time = current_time;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shader_program);
    unsigned int color_uniform = glGetUniformLocation(shader_program, "color");

    glBindVertexArray(VAO);
    glUniform4f(color_uniform, 1.0f, 1.0f, 1.0f, 1.0f);
    for (unsigned int i = 0; i < kSamples; i++)
      glDrawArrays(GL_TRIANGLE_STRIP, 4*i*kPointNodes, 4*kPointNodes);

    glBindVertexArray(VAOd); 
    // draw seg lines
    glUniform4f(color_uniform, 0.0f, 1.0f, 0.0f, 1.0f);
    const unsigned int segments = (unsigned int)(line_segments.size());
    const unsigned int points_vertices = 4*(segments+1)*kPointNodes;
    for (unsigned int i = 0; i < line_segments.size()-1; i++)
      glDrawArrays(GL_TRIANGLE_STRIP,  points_vertices+i*4, 4); 
    // draw last line 
    glUniform4f(color_uniform, 1.0f, 0.0f, 1.0f, 0.1f);
      glDrawArrays(GL_TRIANGLE_STRIP,  points_vertices+(segments-1)*4, 4); 
    // draw seg points
    glUniform4f(color_uniform, 0.0f, 1.0f, 0.0f, 1.0f);
    for (unsigned int i = 0; i < line_segments.size()-1; i++)
      glDrawArrays(GL_TRIANGLE_STRIP, 4*i*kPointNodes, 4*kPointNodes);
    // draw last seg point + mean
    glUniform4f(color_uniform, 1.0f, 0.0f, 1.0f, 0.1f); 
    glDrawArrays(GL_TRIANGLE_STRIP, 4*(segments-1)*kPointNodes, 4*kPointNodes); 
    glDrawArrays(GL_TRIANGLE_STRIP, 4*segments*kPointNodes, 4*kPointNodes);     

    glfwSwapBuffers(window);
    // save 
    if (k != k_saved) 
    {
      GLsizei channels = 3;
		  GLsizei stride = channels * kWinWidth;
		  stride += (stride % 4) ? (4 - stride % 4) : 0;
		  GLsizei bufferSize = stride * kWinHeight;
		  std::vector<char> buffer(bufferSize);
		  glPixelStorei(GL_PACK_ALIGNMENT, 4);
		  glReadBuffer(GL_FRONT);
		  glReadPixels(0, 0, kWinWidth, kWinHeight, GL_RGB, GL_UNSIGNED_BYTE, buffer.data());
		  stbi_flip_vertically_on_write(true);
		  stbi_write_png((out_dir+"/"+std::to_string(k)+std::string(".png")).c_str(),
										  kWinWidth, kWinHeight, channels, buffer.data(), stride);
      std::cout << (out_dir+"/"+std::to_string(k)+std::string(".png")) << "\n";
      k_saved = k;
    }
    glfwPollEvents();
  }

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);
  glDeleteVertexArrays(1, &VAOd);
  glDeleteBuffers(1, &VBOd);
  glDeleteProgram(shader_program);

  glfwTerminate();
  return 0;
}