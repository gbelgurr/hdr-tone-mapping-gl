#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <bits/stdc++.h>
#include <fcntl.h>

#include "utils/io.h"
#include "cpu/cpu_hdr.h"
#include "gpu/opengles_hdr.h"

int main() {
   int width, height;
   float maxSceneLuminance;

   // RgbaInputFile cpu_file("tests/memorial.exr");
   // readEXRMetadata(cpu_file, width, height);
   // cpu_render_scene(cpu_file, width, height);

   RgbaInputFile gpu_file("tests/memorial.exr");
   readEXRMetadata(gpu_file, width, height);
   gl_render_scene(gpu_file, width, height);

   return EXIT_SUCCESS;
}

