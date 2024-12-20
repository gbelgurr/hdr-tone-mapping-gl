#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include <bits/stdc++.h>
#include <fcntl.h>

#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfArray.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>

using namespace OPENEXR_IMF_NAMESPACE;
using namespace IMATH_NAMESPACE;
using namespace std;

struct color_rgba
{
   float	r;
   float	g;
   float	b;
   float	a;
};

/* Shaders */
static const char* vShader = "                  \n\
#version 300 es                                 \n\
precision mediump float;                        \n\
layout (location = 0) in vec3 aPos;             \n\
layout (location = 1) in vec3 aColor;           \n\
layout (location = 2) in vec2 aTexCoord;        \n\
                                                \n\
out vec3 ourColor;                              \n\
out vec2 TexCoord;                              \n\
                                                \n\
void main()                                     \n\
{                                               \n\
	gl_Position = vec4(aPos, 1.0);               \n\
	ourColor = aColor;                           \n\
	TexCoord = vec2(aTexCoord.x, aTexCoord.y);   \n\
}";

static const char* fShader = "                                                                              \n\
#version 300 es                                                                                             \n\
precision highp float;                                                                                      \n\
out vec4 FragColor;                                                                                         \n\
                                                                                                            \n\
in vec3 ourColor;                                                                                           \n\
in vec2 TexCoord;                                                                                           \n\
                                                                                                            \n\
// texture sampler                                                                                          \n\
uniform sampler2D texture1;                                                                                 \n\
                                                                                                            \n\
float luminance(vec3 color)                                                                                 \n\
{                                                                                                           \n\
	return dot(vec3(0.2126f, 0.7152f, 0.0722f), color);                                                      \n\
}                                                                                                           \n\
                                                                                                            \n\
float gamma_correct(float f)                                                                                \n\
{                                                                                                           \n\
    if (f <= 0.0031308f) {                                                                                  \n\
        return f * 12.92f;                                                                                  \n\
    } else {                                                                                                \n\
        return 1.055f * pow(f, 1.0f / 2.4f) - 0.055f;                                                       \n\
    }                                                                                                       \n\
}                                                                                                           \n\
                                                                                                            \n\
void main()                                                                                                 \n\
{                                                                                                           \n\
	float meanBrightness = 0.0290084f;                                                                       \n\
	float maxSceneBrightness = 223.974f;                                                                     \n\
                                                                                                            \n\
	// Convert RGB to luminance values                                                                       \n\
	vec3 in_color = texture(texture1, TexCoord).xyz;                                                         \n\
	float lum = luminance(in_color);                                                                         \n\
                                                                                                            \n\
	// Scaled luminance value                                                                                \n\
	float scaled_lum = lum * (0.18f / meanBrightness);                                                       \n\
                                                                                                            \n\
	// Compression using Reinhard Operator                                                                   \n\
	float whiteness_factor = 1.0f / (maxSceneBrightness * maxSceneBrightness);                               \n\
	float final_lum = (scaled_lum * (1.0f + (scaled_lum * whiteness_factor))) / (1.0f + scaled_lum);         \n\
	float compression_factor = final_lum / scaled_lum;                                                       \n\
	vec3 out_color = in_color * compression_factor;                                                          \n\
                                                                                                            \n\
	// Gamma correction                                                                                      \n\
	float gamma = 1.0f / 2.4f;                                                                               \n\
	FragColor = vec4(clamp(gamma_correct(out_color.r), 0.0f, 1.0f),                                          \n\
                    clamp(gamma_correct(out_color.g), 0.0f, 1.0f),                                          \n\
                    clamp(gamma_correct(out_color.b), 0.0f, 1.0f),                                          \n\
                    1.0f);                                                                                  \n\
}";

static const char* cShader = "                                          \n\
#version 310 es                                                         \n\
precision highp float;                                                  \n\
                                                                        \n\
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;        \n\
layout(rgba32f, binding = 0) uniform readonly highp image2D in_tex;     \n\
layout(rgba32f, binding = 1) uniform writeonly highp image2D out_tex;   \n\
                                                                        \n\
void main() {                                                           \n\
    // get position to read/write data from                             \n\
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);                        \n\
                                                                        \n\
    // get value stored in the image                                    \n\
    vec4 in_val = imageLoad(in_tex, pos);                               \n\
                                                                        \n\
    /* Conversion logic from YUV to RGBA */                             \n\
                                                                        \n\
    // store new value in image                                         \n\
    imageStore(out_tex, pos, in_val);                                   \n\
}";

// Identifiers for the GL objects
GLuint VAO, EBO, VBO, toneMappingShaderProgram, computeShaderProgram, hdrTexture, convertedHdrTexture;

void CreateRectangle()
{
   // Vertex positions for the triangle
   float vertices[] = {
      // positions          // colors           // texture coords
      1.0f,  1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f, // top right
      1.0f, -1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, // bottom right
      -1.0f, -1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f, // bottom left
      -1.0f,  1.0f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f  // top left 
   };

   unsigned int indices[] = {  
      0, 1, 3, // first triangle
      1, 2, 3  // second triangle
   };

   // Specify a VAO for our triangle object
   glGenVertexArrays(1, &VAO);
   glBindVertexArray(VAO);

      // Specify a VBO to bind to the above VAO
      glGenBuffers(1, &VBO);
      glGenBuffers(1, &EBO);

      glBindBuffer(GL_ARRAY_BUFFER, VBO);
      // Loading up the data of the triangle into the VBO
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

         // position attribute
         glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
         glEnableVertexAttribArray(0);
         // color attribute
         glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
         glEnableVertexAttribArray(1);
         // texture coord attribute
         glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
         glEnableVertexAttribArray(2);

      // Unbinding the EBO
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      
      // Unbinding the VBO
      glBindBuffer(GL_ARRAY_BUFFER, 0);

   // Unbinding the VAO
   glBindVertexArray(0);
}

void AddShader(GLuint program, const char* shaderSource, GLenum shaderType)
{
   // Create an empty shader object
   GLuint shader = glCreateShader(shaderType);

   // Convert the source code for the shader 
   // into the right type for loading
   const GLchar* sourceCode[1];
   sourceCode[0] = shaderSource;

   // Convert the length of the source code for 
   // the shader into the right type for loading
   GLint sourceLength[1];
   sourceLength[0] = strlen(shaderSource);

   // Load the shader source into the 
   // empty shader object and compile the code
   glShaderSource(shader, 1, sourceCode, sourceLength);
   glCompileShader(shader);

   // Find and log error if any from 
   // the above compilation process
   GLint result = 0;
   GLchar log[1024] = { 0 };

   glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
   if (!result)
   {
      glGetShaderInfoLog(shader, sizeof(log), NULL, log);
      printf("Error: Compilation of shader of type %d failed, '%s'\n", shaderType, log);
      return;
   }

   // Attach the shader to the shader program
   glAttachShader(program, shader);
}

void CompileShaderProgram()
{
   // Create an empty shader program object
   toneMappingShaderProgram = glCreateProgram();

   // Check if it was created successfully
   if (!toneMappingShaderProgram)
   {
      printf("Error: Generation of shader program failed!\n");
      return;
   }

   // Attaching our vertex and fragment shaders to the shader program
   AddShader(toneMappingShaderProgram, vShader, GL_VERTEX_SHADER);
   AddShader(toneMappingShaderProgram, fShader, GL_FRAGMENT_SHADER);

   // Setting up error logging objects
   GLint result = 0;
   GLchar log[1024] = { 0 };

   // Perform shader program linking
   glLinkProgram(toneMappingShaderProgram);

   // Find and log errors if any from the linking process done above
   glGetProgramiv(toneMappingShaderProgram, GL_LINK_STATUS, &result);
   if (!result)
   {
      glGetProgramInfoLog(toneMappingShaderProgram, sizeof(log), NULL, log);
      printf("Error: Linking of the shader program failed, '%s'\n", log);
      return;
   }

   // Perform shader program validation
   glValidateProgram(toneMappingShaderProgram);

   // Find and log errors if any from the validation process done above
   glGetProgramiv(toneMappingShaderProgram, GL_VALIDATE_STATUS, &result);
   if (!result)
   {
      glGetProgramInfoLog(toneMappingShaderProgram, sizeof(log), NULL, log);
      printf("Error: Shader program validation failed, '%s'", log);
      return;
   }
}

void LoadHDRTexture(int width, int height, Array2D<Rgba> &p) {
   glGenTextures(1, &hdrTexture);
   glBindTexture(GL_TEXTURE_2D, hdrTexture);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

   color_rgba data[height][width];

   for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
         const Rgba& pixel = p[j][i];
         data[j][i].r = pixel.r;
         data[j][i].g = pixel.g;
         data[j][i].b = pixel.b;
         data[j][i].a = pixel.a;
      }
   }

   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, width, height);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, data);
   glBindTexture(GL_TEXTURE_2D, 0);
}

void RunComputeShader(int width, int height)
{
   // Create an empty shader program object
   computeShaderProgram = glCreateProgram();

   // Check if it was created successfully
   if (!computeShaderProgram)
   {
      printf("Error: Generation of shader program failed!\n");
      return;
   }

   // Attaching our vertex and fragment shaders to the shader program
   AddShader(computeShaderProgram, cShader, GL_COMPUTE_SHADER);

   // Setting up error logging objects
   GLint result = 0;
   GLchar log[1024] = { 0 };

   // Perform shader program linking
   glLinkProgram(computeShaderProgram);

   // Find and log errors if any from the linking process done above
   glGetProgramiv(computeShaderProgram, GL_LINK_STATUS, &result);
   if (!result)
   {
      glGetProgramInfoLog(computeShaderProgram, sizeof(log), NULL, log);
      printf("Error: Linking of the shader program failed, '%s'\n", log);
      return;
   }

   // Perform shader program validation
   glValidateProgram(computeShaderProgram);

   // Find and log errors if any from the validation process done above
   glGetProgramiv(computeShaderProgram, GL_VALIDATE_STATUS, &result);
   if (!result)
   {
      glGetProgramInfoLog(computeShaderProgram, sizeof(log), NULL, log);
      printf("Error: Shader program validation failed, '%s'", log);
      return;
   }

   glUseProgram(computeShaderProgram);

   /* Output texture for the compute shader */
   glGenTextures(1, &convertedHdrTexture);
   glBindTexture(GL_TEXTURE_2D, convertedHdrTexture);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, width, height);
   glBindTexture(GL_TEXTURE_2D, 0);

   /* Bind both input and output textures
    * Note: In case of YUV data, upto 3 textures may need to be provided as inputs
    * for multiple planes in the YUV data. In this case, the data is already in RGBA
    * form, so given input is simply redirected to the ouput as it is.
    */
   glBindImageTexture(0, hdrTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
   glBindImageTexture(1, convertedHdrTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

   glDispatchCompute((unsigned int)width, (unsigned int)height, 1);
   glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

// Function to save the rendered image to a file
void gl_save_8bit_image(const char *filename, int width, int height) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to open file for writing");
        exit(EXIT_FAILURE);
    }

    // Allocate buffer to read the pixels
    GLubyte *pixels = (GLubyte*)malloc(width * height * 4);
    if (!pixels) {
        perror("Failed to allocate pixel buffer");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    // Read the pixels from the framebuffer
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Write the PPM header
    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    // Write pixel data to file
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            GLubyte *pixel = pixels + (y * width + x) * 4;
            fwrite(pixel, 1, 3, fp);  // Only write RGB, skip A
        }
    }

    fclose(fp);
    free(pixels);
}

// Function to save the rendered image to a file
void gl_save_10bit_image(const char *filename, int width, int height) {   
   // Allocate buffer to read the pixels
   GLuint *pixels = (GLuint*)malloc(width * height * 4 * 4);
   if (!pixels) {
      perror("Failed to allocate pixel buffer");
   //   fclose(fp);
      exit(EXIT_FAILURE);
   }

   // Read the pixels from the framebuffer
   glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, pixels);

   ofstream outputFile(filename);
   outputFile << "P3\n" << width << ' ' << height << "\n1023\n";

   // Write pixel data to file
   for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
         GLuint *pixel = pixels + (y * width + x);
         GLushort B = (*pixel >> 20) & 0x3FF;
         GLushort G = (*pixel >> 10) & 0x3FF;
         GLushort R = (*pixel >> 0) & 0x3FF;

         outputFile << R << ' ' << G << ' ' << B << '\n';
      }
   }

   outputFile.close();
   free(pixels);
}

bool gl_render_scene(RgbaInputFile &file, int width, int height) {
   // Open the DRM device
   int drm_fd = open("/dev/dri/renderD128", O_RDWR);
   if (drm_fd < 0) {
      perror("Failed to open DRM device");
      return EXIT_FAILURE;
   }

   // Create a GBM device
   struct gbm_device *gbm = gbm_create_device(drm_fd);
   if (!gbm) {
      perror("Failed to create GBM device");
      close(drm_fd);
      return EXIT_FAILURE;
   }

   // Create a GBM surface
   struct gbm_surface *surface = gbm_surface_create(gbm, width, height, GBM_FORMAT_XRGB2101010, GBM_BO_USE_RENDERING);
   if (!surface) {
      perror("Failed to create GBM surface");
      gbm_device_destroy(gbm);
      close(drm_fd);
      return EXIT_FAILURE;
   }

   // Get an EGL display connection
   EGLDisplay egl_display = eglGetDisplay(gbm);
   if (egl_display == EGL_NO_DISPLAY) {
      perror("Failed to get EGL display");
      gbm_surface_destroy(surface);
      gbm_device_destroy(gbm);
      close(drm_fd);
      return EXIT_FAILURE;
   }

   // Initialize the EGL display connection
   if (!eglInitialize(egl_display, NULL, NULL)) {
      perror("Failed to initialize EGL");
      eglTerminate(egl_display);
      gbm_surface_destroy(surface);
      gbm_device_destroy(gbm);
      close(drm_fd);
      return EXIT_FAILURE;
   }

   // Choose an appropriate EGL configuration
   EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 10,
      EGL_GREEN_SIZE, 10,
      EGL_BLUE_SIZE, 10,
      EGL_ALPHA_SIZE, 2,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_NONE
   };

   EGLConfig config;
   EGLint num_configs;
   if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs)) {
      perror("Failed to choose EGL config");
      eglTerminate(egl_display);
      gbm_surface_destroy(surface);
      gbm_device_destroy(gbm);
      close(drm_fd);
      return EXIT_FAILURE;
   }

   if (!eglBindAPI(EGL_OPENGL_ES_API)) {
         perror("Failed to OpenGL ES API");
         eglTerminate(egl_display);
         gbm_surface_destroy(surface);
         gbm_device_destroy(gbm);
         close(drm_fd);
         return EXIT_FAILURE;
   };

   // Create an EGL context
   EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
   };

   EGLContext context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
   if (context == EGL_NO_CONTEXT) {
      perror("Failed to create EGL context");
      eglTerminate(egl_display);
      gbm_surface_destroy(surface);
      gbm_device_destroy(gbm);
      close(drm_fd);
      return EXIT_FAILURE;
   }

   // Create an EGL window surface
   EGLSurface egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)surface, NULL);
   if (egl_surface == EGL_NO_SURFACE) {
      perror("Failed to create EGL surface");
      eglDestroyContext(egl_display, context);
      eglTerminate(egl_display);
      gbm_surface_destroy(surface);
      gbm_device_destroy(gbm);
      close(drm_fd);
      return EXIT_FAILURE;
   }

   // Make the context and surface current
   if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, context)) {
      perror("Failed to make EGL context current");
      eglDestroySurface(egl_display, egl_surface);
      eglDestroyContext(egl_display, context);
      eglTerminate(egl_display);
      gbm_surface_destroy(surface);
      gbm_device_destroy(gbm);
      close(drm_fd);
      return EXIT_FAILURE;
   }

   // Set up the viewport
   glViewport(0, 0, width, height);
   CreateRectangle();
   CompileShaderProgram();

   Array2D<Rgba> hdr_pixels(height, width);
   readPixels(file, hdr_pixels, width, height);
   LoadHDRTexture(width, height, hdr_pixels);
   RunComputeShader(width, height);

   glBindTexture(GL_TEXTURE_2D, convertedHdrTexture);

   /* Read the converted texture for avg scene brightness and max scene brightness */
   GLfloat compute_converted_pixels[width * height * 4];
   GLuint fbo;

   glGenFramebuffers(1, &fbo); 
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, convertedHdrTexture, 0);
   glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, compute_converted_pixels);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glDeleteFramebuffers(1, &fbo);

   double totalLuminance = 0.0;
   float max_scene_brightness = std::numeric_limits<float>::min();

   for (int i = 0; i < width * height * 4; i = i + 4)
   {
      float lum = 0.2126f * compute_converted_pixels[i] +
                  0.7152f * compute_converted_pixels[i + 1] +
                  0.0722f * compute_converted_pixels[i + 2];

      if (lum > max_scene_brightness)
         max_scene_brightness = lum;
      
      totalLuminance += log(lum);
   }

   float avg_scene_brightness = static_cast<float>(exp(totalLuminance / (width * height)));
   cout << "Maximum Scene Brightness: " << max_scene_brightness << endl;
   cout << "Average Scene Brightness: " << avg_scene_brightness << endl;

   // Clear the window
   glClearColor(0.3f, 0.5f, 0.6f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

      // Activate the required shader for drawing
      glUseProgram(toneMappingShaderProgram);
         // Bind the required object's VAO
         glBindVertexArray(VAO);
         glBindBuffer(GL_ARRAY_BUFFER, VBO);
         glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

            // Perform the draw call to initialise the pipeline
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

         glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
         glBindBuffer(GL_ARRAY_BUFFER, 0);
            
         // Unbinding just for completeness
         glBindVertexArray(0);
      // Deactivating shaders for completeness
      glUseProgram(0);

   // Swap the buffers to display the rendered image
   eglSwapBuffers(egl_display, egl_surface);

   // Save the rendered image to a file
   gl_save_10bit_image("reinhard-extended-chapel-with-gamma-correction-10bit.ppm", width, height);

   glDeleteVertexArrays(1, &VAO);
   glDeleteBuffers(1, &VBO);
   glDeleteBuffers(1, &EBO);

   // Clean up
   eglDestroySurface(egl_display, egl_surface);
   eglDestroyContext(egl_display, context);
   eglTerminate(egl_display);
   gbm_surface_destroy(surface);
   gbm_device_destroy(gbm);
   close(drm_fd);

   return EXIT_SUCCESS;
}
