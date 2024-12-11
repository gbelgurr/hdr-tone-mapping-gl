.PHONY: clean
exr-tone-mapping: main.cpp vertex.glsl fragment.glsl
	g++ -g main.cpp -o exr-tone-mapping -lOpenEXR -lImath -I/usr/include/Imath -lGLESv2 -lgbm -lEGL

clean:
	rm -rf *.ppm
