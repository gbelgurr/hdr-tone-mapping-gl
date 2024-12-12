#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfArray.h>

using namespace OPENEXR_IMF_NAMESPACE;
using namespace IMATH_NAMESPACE;
using namespace std;

void readEXRMetadata(RgbaInputFile &file, int &width, int &height) {
   // Get the header and data window
   // const Header& header = file.header();
   const Box2i& dw = file.dataWindow();

   // Compute dimensions
   width = dw.max.x - dw.min.x + 1;
   height = dw.max.y - dw.min.y + 1;

   // cout << "Width: " << width << ", Height: " << height << endl;
}

void readPixels(RgbaInputFile &file, Array2D<Rgba> &p, int width, int height) {
   const Box2i& dw = file.dataWindow();

   // Assuming the file contains RGBA channels
   // Read pixel data
   file.setFrameBuffer(&p[0][0] - dw.min.x - dw.min.y * width, 1, width);
   file.readPixels(dw.min.y, dw.max.y);
}
