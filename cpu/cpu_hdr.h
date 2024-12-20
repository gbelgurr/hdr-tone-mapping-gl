#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfArray.h>

using namespace OPENEXR_IMF_NAMESPACE;
using namespace IMATH_NAMESPACE;
using namespace std;

void clampPixels(Array2D<Rgba> &p, int width, int height) {
   for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
         Rgba& pixel = p[y][x];
         half low = 0.0f;
         half high = 1.0f;
         pixel.r = clamp(pixel.r, low, high);
         pixel.g = clamp(pixel.g, low, high);
         pixel.b = clamp(pixel.b, low, high);
      }
   }
}

void computeLuminance(Array2D<Rgba> &p, float *scene_luminance, int width, int height) {
   for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
         const Rgba& pixel = p[y][x];
         float luminance = 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
         *((scene_luminance + y * width) + x) = luminance;
      }
   }
}

void computeSpecialBrightnessValues(float *scene_luminance, int width, int height,
                                    float &max_scene_brightness,
                                    float &avg_scene_brightness) {
   // Compute luminance and average
   double totalLuminance = 0.0;
   int totalPixels = width * height;

   for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
            const float luminance = *((scene_luminance + y * width) + x);
            if (luminance > max_scene_brightness)
               max_scene_brightness = luminance;
            totalLuminance += log(luminance);
      }
   }

   avg_scene_brightness = static_cast<float>(exp(totalLuminance / totalPixels));
   cout << "Maximum Scene Brightness: " << max_scene_brightness << endl;
   cout << "Average Scene Brightness: " << avg_scene_brightness << endl;
}

void scaleLuminances(float *scene_luminance, float avg_scene_brightness, int width, int height) {
   float scaling_factor = 0.18f / avg_scene_brightness;

   for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
         *((scene_luminance + y * width) + x) *= scaling_factor;
      }
   }
}

void compressLuminances(Array2D<Rgba> &p, float *scene_luminance, float max_scene_brightness, int width, int height) {
   float whiteness_factor = 1.0f / (max_scene_brightness * max_scene_brightness);

   for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
         float input_luminance = *((scene_luminance + y * width) + x);
         float output_luminance = 
            (input_luminance * (1.0f + (input_luminance * whiteness_factor))) / (1.0f + input_luminance);
         float compression_factor = output_luminance / input_luminance;

         Rgba& pixel = p[y][x];
         pixel.r *= compression_factor;
         pixel.g *= compression_factor;
         pixel.b *= compression_factor;
      }
   }
}

void correctGamma(Array2D<Rgba> &p, int width, int height) {
   float gamma = 1.0f / 2.2f;

   for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
         Rgba& pixel = p[y][x];
         pixel.r = pow(pixel.r, gamma);
         pixel.g = pow(pixel.g, gamma);
         pixel.b = pow(pixel.b, gamma);
      }
   }
}

void cpu_save_8bit_image(const char name[], Array2D<Rgba> &p, int width, int height) {
   ofstream outputFile(name);
   outputFile << "P3\n" << width << ' ' << height << "\n255\n";

   for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
         const Rgba& pixel = p[j][i];
         int ir = int(255.999 * pixel.r);
         int ig = int(255.999 * pixel.g);
         int ib = int(255.999 * pixel.b);

         outputFile << ir << ' ' << ig << ' ' << ib << '\n';
      }
   }

   outputFile.close();
}

void cpu_save_10bit_image(const char name[], Array2D<Rgba> &p, int width, int height) {
   ofstream outputFile(name);
   outputFile << "P3\n" << width << ' ' << height << "\n1023\n";

   for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
         const Rgba& pixel = p[j][i];
         int ir = int(1023.999 * pixel.r);
         int ig = int(1023.999 * pixel.g);
         int ib = int(1023.999 * pixel.b);

         outputFile << ir << ' ' << ig << ' ' << ib << '\n';
      }
   }

   outputFile.close();
}

void reinhard_extended_algorithm(Array2D<Rgba> &pixels, int width, int height) {
   float scene_luminance[width][height], max_scene_brightness = std::numeric_limits<float>::min(), avg_scene_brightness = 0.0f;
   computeLuminance(pixels, (float*)scene_luminance, width, height);
   computeSpecialBrightnessValues((float*)scene_luminance, width, height, max_scene_brightness, avg_scene_brightness);
   scaleLuminances((float*)scene_luminance, avg_scene_brightness, width, height);
   compressLuminances(pixels, (float*)scene_luminance, max_scene_brightness, width, height);
}

void cpu_render_scene(RgbaInputFile &file, int width, int height) {
   Array2D<Rgba> clamped_pixels(height, width);
   readPixels(file, clamped_pixels, width, height);
   clampPixels(clamped_pixels, width, height);
   cpu_save_8bit_image("clamped-chapel-without-gamma-correction-8bit.ppm", clamped_pixels, width, height);
   cpu_save_10bit_image("clamped-chapel-without-gamma-correction-10bit.ppm", clamped_pixels, width, height);
   correctGamma(clamped_pixels, width, height);
   cpu_save_8bit_image("clamped-chapel-with-gamma-correction-8bit.ppm", clamped_pixels, width, height);
   cpu_save_10bit_image("clamped-chapel-with-gamma-correction-10bit.ppm", clamped_pixels, width, height);

   Array2D<Rgba> reinhard_pixels(height, width);
   readPixels(file, reinhard_pixels, width, height);
   reinhard_extended_algorithm(reinhard_pixels, width, height);
   correctGamma(reinhard_pixels, width, height);
   cpu_save_8bit_image("reinhard-extended-chapel-with-gamma-correction-8bit.ppm", reinhard_pixels, width, height);
   cpu_save_10bit_image("reinhard-extended-chapel-with-gamma-correction-10bit.ppm", reinhard_pixels, width, height);
}